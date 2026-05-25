---
date: "2026-05-24"
title: 'ARM TF-A Day 1: Introduction to Trusted Firmware-A and The Execution Architecture'
thumbnail: "/posts/arm-tfa-day1-intro/thumbnail.png"
author: "Mbharmal"
product: "embedded-engineering"
tags:
  - "ARM64"
  - "TF-A"
  - "Bootloader"
  - "Firmware"
categories:
  - "arm-tfa"
---

Hey there! Welcome to a brand new deep-dive series. If you followed my previous `arm64*` exception levels and bare-metal series, you know I love getting into the weeds of how things actually work at the silicon level. 

In this series, we are going to tackle **ARM Trusted Firmware-A (TF-A)**. We will take the official TF-A source code, dissect its assembly, trace its memory mappings, and watch it boot from the first breath of the CPU all the way to handing over control to an Operating System.

<!--more-->

## Motivation: Why do we need TF-A?

Modern ARMv8-A and ARMv9-A processors are complex beasts. They don't just boot up and run code; they boot up into a highly structured, isolated environment governed by ARM's TrustZone technology. 

TrustZone splits the system into two distinct worlds:
1.  **The Secure World:** Where highly trusted code lives. This is the bedrock of system security. Real-world use cases include:
    *   **Digital Rights Management (DRM):** Processing and decoding premium video streams (like Netflix 4K) securely so that the keys and raw video frames are never exposed to a potentially compromised Android OS.
    *   **Biometric Authentication:** Verifying your face ID or fingerprint. The raw sensor data is routed directly to the secure world, verified against a secure template, and only a simple "Yes/No" token is passed back to the OS.
    *   **Secure Payment Gateways:** Protecting cryptographic keys used for Apple Pay or Google Wallet transactions.
2.  **The Non-Secure (Normal) World:** Where your everyday OS (like Linux or Android) and user applications run.

To manage this split, the system needs a gatekeeper—a piece of software that runs at the absolute highest privilege level (Exception Level 3, or **EL3**) to route interrupts, manage power (PSCI), and facilitate communication between the two worlds. 

That gatekeeper is **Trusted Firmware-A (TF-A)**. It provides a reference implementation of secure world software for ARM processors.

## The Chain of Trust: Boot Stages Overview

TF-A isn't just one big monolithic binary. It's broken down into specific boot stages, often referred to as "Bootloader" (BL) stages. They execute in a specific sequence to establish a **Chain of Trust**. This ensures that every subsequent piece of software is verified and authenticated before it is allowed to run.

Here is the standard ARM boot sequence:

1.  **BL1 (AP Trusted ROM):** The root of trust. This is the very first code that runs out of read-only memory (ROM) when the CPU comes out of reset. 
    *   *Why is it needed?* Because the CPU wakes up blind and amnesiac. There is no RAM, no caches, and no security state. BL1 is baked into the silicon itself; it cannot be modified by malware. Its sole purpose is to securely verify the very first writable piece of firmware (BL2) before allowing the system to proceed. It guarantees the chain of trust starts from an unalterable anchor.
2.  **BL2 (Trusted Boot Firmware):** Platform initialization. 
    *   *Why is it needed? Why can't BL1 do this?* BL1 is burned into physical ROM, meaning its size is strictly limited (often just a few kilobytes) and it can never be updated to fix bugs or support new memory chips. BL2, however, is loaded into writable Secure SRAM. BL2 contains the complex, easily-updatable logic required to train the main DRAM (which varies wildly between different board designs) and the complex parsing logic to extract the remaining firmware payloads from storage. BL1 is the tiny anchor; BL2 is the heavy lifter.
3.  **BL31 (EL3 Runtime Software):** The Secure Monitor. This is the core of TF-A that stays resident in memory. It handles context switching between the Secure and Normal worlds.
4.  **BL32 (Secure-EL1 Payload):** A Trusted OS (like OP-TEE). 
    *   *What is its role?* If BL31 is the secure "hypervisor" routing messages, BL32 is the secure "operating system." In real life, OP-TEE provides a miniature, isolated OS environment with its own scheduler and memory management. 
    *   *Who loads S-EL0 apps?* OP-TEE itself! Just like Linux loads user apps into EL0, OP-TEE loads Trusted Applications (TAs) into Secure-EL0 (S-EL0). When an Android app needs to decode a DRM video, it asks OP-TEE to launch the specific Widevine DRM TA in S-EL0. OP-TEE manages that execution entirely hidden from Android.
5.  **BL33 (Non-Trusted Firmware):** The Normal world bootloader (like U-Boot or UEFI).
    *   *Wait, why do we need U-Boot? Why not boot Linux directly?* The Linux kernel is a massive piece of software and it expects the hardware to be in a very specific, standardized state before it executes (e.g., networking initialized, device trees passed in memory, generic timers configured). BL31 and BL32 only care about the *Secure* world. BL33 is required to initialize the complex *Non-Secure* peripherals (like USB controllers, PCIe buses, or Ethernet MACs) and fetch the massive Linux kernel from a network (PXE) or complex filesystem (ext4) which TF-A is just too lightweight to understand.

## Execution Contexts: Mapping Stages to Exception Levels

If you recall from our `arm64-day0-exception-levels` post, ARM utilizes Exception Levels (EL0-EL3). Let's map our TF-A boot stages to these levels to understand their privileges.

*   **EL3 (Highest Privilege):** Both **BL1** and **BL31** run here. They have absolute control over the system and manage the secure/non-secure states.
*   **Secure-EL1 (S-EL1):** **BL2** and **BL32** run here. They operate in the Secure world but are slightly restricted compared to EL3. 
    *   *(Note: TF-A strongly encourages running BL2 at S-EL1 to adhere to the principle of least privilege—minimizing code running at the absolute highest privilege EL3. For example, standard deployments on SoCs like the **STMicroelectronics STM32MP1** or **NXP's Layerscape** series execute BL2 at S-EL1, utilizing a tiny EL3 stub just for the transitions).*
*   **EL2 / EL1 (Normal World):** **BL33** (U-Boot/UEFI) runs at EL2 or EL1, preparing the environment for the Rich OS (Linux).

## The Setup: Exploring the TF-A Source Code

Throughout this series, we will be referencing the official TF-A source tree. We'll primarily use **ARM QEMU (Virt machine)** to trace the standard BL1 -> BL2 -> BL31 flow, as QEMU perfectly emulates the complete reference architecture.

*Note: Later in the series, we'll pivot to a real Raspberry Pi 5 to see how TF-A adapts to proprietary, "weird" boot flows where the GPU boots first!*

If you clone the TF-A repository (`git clone https://review.trustedfirmware.org/TF-A/trusted-firmware-a`), you'll see a structure like this:

```bash
trusted-firmware-a/
├── bl1/               # BL1 (Trusted ROM) source code
├── bl2/               # BL2 (Trusted Boot Firmware) source code
├── bl31/              # BL31 (EL3 Runtime Software / Secure Monitor)
├── bl32/              # BL32 (Secure-EL1 Payloads like SP_MIN)
├── docs/              # Official documentation
├── drivers/           # GIC, timers, UART, and console drivers
├── include/           # Header files (architectural definitions, SMCCC)
├── lib/               # Common libraries (libc, translation tables/xlat)
├── plat/              # Platform specific ports (QEMU, ARM FVP, Raspberry Pi)
└── services/          # Standard services (PSCI, SPD)
```

In the next article, we will open up `bl1/aarch64/bl1_entrypoint.S`. We'll look at the exact assembly instruction that executes when the CPU is powered on, how the cache is disabled, and how the C runtime is established at EL3.

I hope this gives you a good mental map of what we are about to tackle. Buckle up, it's going to be a fun, low-level ride. I will see you in the next one!
