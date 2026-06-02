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
    *   **Digital Rights Management (DRM):** Processing and decoding premium video streams (like Netflix 4K) securely. On ARM-A devices running Android, technologies like Widevine L1 decrypt media keys inside the Secure World TEE. The decrypted video frames are written into secure memory buffers protected by hardware firewalls (e.g., ARM TrustZone Address Space Controller or TZASC) so that the Normal World OS cannot read or copy them; they are routed directly to the display controller.
    *   **Biometric Authentication:** Verifying your face or fingerprint. On Android, the biometric sensor's physical bus (like SPI or I2C) is dynamically firewalled so that only the Secure World can access it. The raw biometric data is captured, processed, and matched against stored templates entirely inside the TEE. Instead of a simple "Yes/No", the TEE returns a cryptographically signed authentication token (AuthToken) to the Normal World OS. *(Note: While Android uses TrustZone on the main CPU cores, Apple devices handle this via the Secure Enclave Processor (SEP)—a physically isolated hardware coprocessor with its own microkernel, rather than standard main-CPU TrustZone.)*
    *   **Secure Payment Gateways:** Protecting cryptographic keys and transaction logic. On Android, Google Wallet / Google Pay stores and uses transaction keys via Keymaster/KeyMint running as Trusted Applications inside the TEE. Some modern devices also offload this to a dedicated, tamper-resistant Secure Element (StrongBox). *(Note: Apple Pay does not use main-CPU TrustZone; it stores payment credentials inside a dedicated Secure Element (SE) chip, and uses the Secure Enclave (SEP) coprocessor to authorize transactions.)*
2.  **The Non-Secure (Normal) World:** Where your everyday OS (like Linux or Android) and user applications run.

To manage this split, the system needs a gatekeeper—a piece of software that runs at the absolute highest privilege level (Exception Level 3, or **EL3**) to route interrupts, manage power (PSCI), and facilitate communication between the two worlds. 

That gatekeeper is **Trusted Firmware-A (TF-A)**. It provides a reference implementation of secure world software for ARM processors.

## The Chain of Trust: Boot Stages Overview

TF-A isn't just one big monolithic binary. It's broken down into specific boot stages, often referred to as "Bootloader" (BL) stages. They execute in a specific sequence to establish a **Chain of Trust**. This ensures that every subsequent piece of software is verified and authenticated before it is allowed to run.

Here is the standard ARM boot sequence:

1.  **BL1 (AP Trusted ROM / AP Boot ROM):** The root of trust. This is the very first TF-A code that runs when the CPU comes out of reset. It executes out of read-only memory, either ROM burned into silicon or NOR flash — depending on the SoC design.
    *   *Why is it needed?* Because the CPU wakes up blind and amnesiac. There is no RAM, no caches, and no security state configured. BL1's sole purpose is to securely set up the minimum environment, verify the very first writable piece of firmware (BL2) using a cryptographic chain of trust, and hand off. It guarantees the chain starts from an unalterable anchor — whether that anchor is the silicon BootROM below it or BL1 itself when stored in ROM.

    *   *Important distinction:* Many SoCs have a **silicon BootROM** — immutable code physically burned into the die at manufacture by the silicon vendor — that runs *before* TF-A's BL1 even executes. That silicon BootROM verifies and loads BL1. On other platforms, BL1 IS the first code out of reset. We'll explore this topology in depth in later articles.

2.  **BL2 (AP RAM Firmware / Trusted Boot Firmware):** Platform initialization.
    *   *Why is it needed? Why can't BL1 do this?* BL1's footprint is deliberately kept tiny — it runs from ROM or a small, trusted region. It can never be updated in the field because it sits above the chain-of-trust anchor. BL2, however, is loaded into writable Secure SRAM and is fully updatable. BL2 contains the complex, easily-updatable logic required to train the main DRAM (which varies wildly between board designs) and the complex parsing logic to extract the remaining firmware payloads from storage. BL1 is the tiny anchor; BL2 is the heavy lifter.
    *   BL2 runs at **Secure-EL1 (S-EL1)** on standard platforms. Some platforms use `RESET_TO_BL2` to skip BL1 entirely — in that case BL2 itself runs at EL3. We'll cover this in Day 3.

3.  **BL31 (EL3 Runtime Software / Secure Monitor):** The permanent resident. This is the TF-A component that stays loaded in memory for the entire lifetime of the system. It handles:
    *   **Context switching** between the Secure and Normal worlds on every SMC (Secure Monitor Call).
    *   **PSCI** (Power State Coordination Interface) — the standard API for CPU idle, hotplug, and system suspend/resume used by Linux.
    *   **SMCCC** (SMC Calling Convention) dispatch — routing SMC calls from both worlds to the right handler.
    *   **GIC configuration** — setting up the interrupt controller so both worlds receive the right interrupts.

    Note that BL1 and BL31 both run at EL3, but they do so at **different times**. BL1 occupies EL3 during boot only, then is discarded. BL31 takes over EL3 at runtime, typically reusing the memory BL1 occupied. They are never both resident simultaneously.

4.  **BL32 (Secure-EL1 Payload):** A Trusted OS or Secure Partition Manager.
    *   *What is its role?* BL32 provides runtime secure services to the normal world. On classic deployments, this is a Trusted OS like **OP-TEE**.
    *   *What EL does it run at?* This depends on the platform. In the classic, single-TEE configuration, BL32 runs at **Secure-EL1 (S-EL1)**. However, on ARMv9-A systems with the `FEAT_SEL2` (Secure EL2) extension, an **SPMC** (Secure Partition Manager Core) like **Hafnium** can run at **S-EL2**, with OP-TEE and other Trusted Applications below it at S-EL1/S-EL0. The BL32 label is officially intended for single-TEE systems; multi-partition systems use the more general **FF-A** (Firmware Framework for A-profile) terminology.
    *   *Who loads S-EL0 apps?* OP-TEE itself! Just like Linux loads user apps into EL0, OP-TEE loads Trusted Applications (TAs) into Secure-EL0 (S-EL0). When an Android app needs to decode a DRM video, it asks OP-TEE to launch the specific Widevine DRM TA in S-EL0. OP-TEE manages that execution entirely hidden from Android.
    *   *Does TF-A ship its own BL32?* Yes! TF-A includes three built-in BL32 options in `bl32/`:
        *   **`tsp/`** — Test Secure Payload (TSP): A minimal BL32 used for development and testing of the BL31 dispatch infrastructure.
        *   **`sp_min/`** — A minimal Secure Monitor for AArch32 platforms where a full BL31 isn't available.
        *   **`optee/`** — An integration shim for OP-TEE.

5.  **BL33 (AP Normal World Firmware / Non-Trusted Firmware):** The Normal world bootloader (like U-Boot or UEFI).
    *   *Why do we need U-Boot? Why not boot Linux directly?* The Linux kernel is a massive piece of software and it expects the hardware to be in a very specific, standardized state before it executes (e.g., device trees passed in memory, generic timers configured, a specific register state). BL33 bridges this gap: it fetches the Linux kernel from wherever it lives (network via PXE, eMMC ext4 partition, etc.), sets up the device tree, and jumps to the kernel entry point.
    *   BL33 typically runs at **EL2** on modern systems — the hypervisor level. EL1 is reserved for the Linux kernel itself. On systems without a hypervisor, BL33 may configure the system and drop directly to EL1, but EL2 is the standard target.

### A Note on the 4-World Future: CCA and RMM

The classic 2-world (Secure/Non-Secure) model described above is the baseline. ARM's **Confidential Compute Architecture (CCA)**, introduced with ARMv9-A, adds a third and fourth world:

- **Realm World** at `Realm-EL2`: Managed by the **RMM (Realm Monitor Management Firmware)**, this allows cloud workloads to run in hardware-isolated "Realms" that are protected even from the hypervisor and the OS.
- **Root World** at EL3: A new concept in CCA where EL3 itself is an isolated root world separate from Secure.

The boot flow we'll trace in this series is the classic 3-stage (BL1→BL2→BL31) flow. CCA introduces the `RMM` as an additional image loaded by BL2. We'll leave CCA for later in the series.

## Execution Contexts: Mapping Stages to Exception Levels

If you recall from our `arm64-day0-exception-levels` post, ARM utilizes Exception Levels (EL0-EL3). Let's map our TF-A boot stages to these levels to understand their privileges.

*   **EL3 (Highest Privilege):** **BL1** runs here *during boot only*, then is discarded. **BL31** runs here permanently *at runtime*. They never coexist — BL31 reuses the EL3 memory that BL1 occupied. Both have absolute control over the system and manage the secure/non-secure states.
*   **Secure-EL2 (S-EL2):** On systems with `FEAT_SEL2`, the **SPMC** (e.g., Hafnium) may run here, acting as a secure hypervisor for multiple Trusted Applications.
*   **Secure-EL1 (S-EL1):** **BL2** runs here during boot. **BL32** (OP-TEE or another TEE) runs here at runtime. They operate in the Secure world but with less privilege than EL3.
    *   *(Note: TF-A strongly encourages running BL2 at S-EL1 to adhere to the principle of least privilege — minimizing code running at EL3. Standard deployments on SoCs like the **STMicroelectronics STM32MP1** or **NXP's Layerscape** series execute BL2 at S-EL1, using a tiny EL3 stub just for the transitions.)*
*   **EL2 / EL1 (Normal World):** **BL33** (U-Boot/UEFI) runs at EL2, preparing the environment for the Rich OS (Linux), which then runs at EL1.

## The Setup: Exploring the TF-A Source Code

Throughout this series, we will be referencing the official TF-A source tree. We'll primarily use **ARM QEMU (Virt machine)** to trace the standard BL1 → BL2 → BL31 flow, as QEMU perfectly emulates the complete reference architecture.

*Note: Later in the series, we'll pivot to a real Raspberry Pi 5 to see how TF-A adapts to proprietary, "weird" boot flows where the GPU boots first!*

If you clone the TF-A repository (`git clone https://review.trustedfirmware.org/TF-A/trusted-firmware-a`), you'll see a structure like this:

```bash
trusted-firmware-a/
├── bl1/               # BL1 (AP Boot ROM) source code
├── bl2/               # BL2 (Trusted Boot Firmware) source code
├── bl2u/              # BL2U (Firmware Update agent)
├── bl31/              # BL31 (EL3 Runtime Firmware / Secure Monitor)
├── bl32/              # BL32 payloads: tsp/ (test), sp_min/ (AArch32), optee/
├── common/            # Shared boot and utility code
├── docs/              # Official documentation
├── drivers/           # GIC, timers, UART, crypto and console drivers
├── fdts/              # Firmware device tree sources
├── include/           # Header files (architectural definitions, SMCCC)
├── lib/               # Common libraries (libc, translation tables/xlat, EL3 runtime)
├── plat/              # Platform-specific ports (QEMU, ARM FVP, Raspberry Pi, ...)
├── services/          # Firmware services: std_svc/ (PSCI), spd/ (SPD), el3/ (SPMC), ...
└── tools/             # Host tools: fiptool, cert_create, sptool
```

In the next article, we will open up `bl1/aarch64/bl1_entrypoint.S`. We'll look at the exact assembly instruction that executes when the CPU is powered on, how the C runtime is established at EL3, and how secondary cores are handled.

I hope this gives you a good mental map of what we are about to tackle. I will see you in the next one!
