---
date: "2025-06-08"
title: 'System on Chip (SoC) and Bare-Metal Programming'
thumbnail: "/posts/soc/cover.jpg"
author: "Mbharmal"

product: "embedded-engineering"

tags:
  - "system-on-chip"
  - "soc"

categories:
  - "soc"
---

A System on Chip (SoC) is an integrated circuit that combines multiple components of a computer or electronic system into a single chip.

<!--more-->

Unlike traditional setups where the CPU, memory, and peripherals are separate and connected via a motherboard, an SoC integrates these into a compact, power-efficient package.

## Key Components of an SoC
- **CPU (Central Processing Unit):** The core processing unit, often a multi-core processor like the ARM Cortex-A series in AArch64.
- **GPU (Graphics Processing Unit):** Manages graphics rendering, essential for systems with displays.
- **Memory:** Includes on-chip RAM, caches, or interfaces to external memory like DDR RAM.
- **Peripherals:** Encompasses GPIO, UART, I2C, SPI, USB controllers, and more.
- **Interconnect:** Uses buses like AMBA to link components.
- **Power Management Unit (PMU):** Optimizes power states for efficiency.

SoCs are ideal for devices like smartphones, IoT gadgets, and the Raspberry Pi due to their: Compact size and low power consumption, Cost-effectiveness for mass production and, High customizability for applications like automotive or medical devices.

As an example, the Raspberry Pi 4’s Broadcom BCM2711 SoC integrates a quad-core ARM Cortex-A72 CPU, a VideoCore VI GPU, and various peripherals, tailored for a single-board computer.

## AArch64 Architecture and Its Relevance

AArch64 is the 64-bit execution state of the ARMv8-A architecture, widely used in embedded systems, servers, and mobile devices for its efficiency and scalability.

### Key Features of AArch64
- **64-bit Registers:** 31 general-purpose registers (X0-X30), each 64 bits wide, plus a stack pointer (SP) and program counter (PC). There are also several Special Function and Configuration registers.
- **Instruction Set:** Utilizes a RISC design for simplicity and power efficiency.
- **Memory Addressing:** Supports up to 48-bit virtual addresses, extendable to 52-bit in some cases.
- **Exception Levels (EL0-EL3):** Defines privilege levels for user applications (EL0), OS kernel (EL1), hypervisor (EL2), and secure monitor (EL3).
- **Advanced SIMD (NEON):** Enables parallel processing for multimedia and signal processing.

### Relevance to Embedded Systems
- Power efficiency suits battery-powered devices.
- Scalability supports low-end IoT to high-performance systems like the Raspberry Pi 4.
- Open ecosystem with robust toolchain support (e.g., GCC, LLVM).

> The BCM2711 SoC uses four Cortex-A72 cores in AArch64 mode, making it an excellent platform for exploring this architecture.

## General Boot Sequence in Embedded Systems

The boot sequence of an embedded system with an SoC is the process from power-on to handing control to an operating system or application.
1. **Power-On Reset (POR):** The SoC’s reset circuitry initializes the CPU and components to a known state, stabilizing clocks and voltage regulators.
1. **On-Chip Boot ROM:** The CPU executes code from a hardwired Boot ROM, loading a bootloader from a predefined location (e.g., flash memory, SD card) and configuring basic hardware.
1. **First-Stage Bootloader (FSBL):** Loaded from external storage, it initializes critical peripherals like DRAM and sets up a minimal execution environment.
1. **Second-Stage Bootloader (SSBL):** A more advanced loader (e.g., U-Boot) configures additional peripherals, loads the kernel, and passes control to it.
1. **Kernel/OS Initialization:** The operating system (e.g., Linux) initializes drivers and user space.

> Each stage enables more hardware features, critical for bare-metal programming at the FSBL or SSBL level.

The Raspberry Pi 4’s boot process is unique due to its Broadcom BCM2711 SoC and SD card reliance.
1. **Power-On and GPU Boot:** The VideoCore IV GPU boots first from an on-chip Boot ROM, loading `bootcode.bin` from the SD card’s FAT32 partition into the GPU’s L2 cache.
1. **GPU Loads Firmware:** `bootcode.bin` loads `start.elf` (GPU firmware) into RAM, which initializes DRAM and reads `config.txt` for settings like overclocking.
1. **ARM CPU Activation:** The GPU loads the ARM kernel image (e.g., `kernel8.img` for AArch64) into RAM and releases the Cortex-A72 cores to execute at address 0x80000.
1. **Kernel Execution:** The ARM CPU runs the Linux kernel or bare-metal code.

>> The GPU’s dominance in booting is unusual, reflecting the Raspberry Pi’s graphics-focused educational roots.

The SoC datasheet is the definitive guide for understanding its hardware. For the BCM2711, Broadcom offers limited documentation, but concepts like memory-mapped I/O (MMIO) are universal. The data sheet has details of -
- Register addresses for peripherals (e.g., GPIO, UART).
- Memory map detailing RAM, ROM, and peripheral locations.
- Pin multiplexing options and electrical characteristics.

Peripherals are controlled by reading/writing to specific memory addresses. For example, writing to address 0x3F200000 on the BCM2711 toggles a GPIO pin. The CPU sees these addresses as part of its memory space, mapped via the SoC’s interconnect.

> MMIO requires precise register knowledge from the datasheet. Incorrect writes can crash the system, making datasheet study essential for bare-metal coding.

## Peripherals extend SoC functionality
- **GPIO (General Purpose Input/Output):** Configurable pins for interfacing with LEDs, buttons, or sensors, controlled via MMIO registers. The Raspberry Pi 4 has 28 GPIO pins on its 40-pin header.
- **UART (Universal Asynchronous Receiver/Transmitter):** A serial communication interface for debugging or device interaction, mapped at 0x3F201000 on the Raspberry Pi 4.
- **Interrupts:** Hardware signals that pause the CPU for urgent events, managed via an interrupt controller (e.g., GIC-400 in AArch64 SoCs).

> Misconfigured interrupts can cause “interrupt storms,” overwhelming the CPU. Disable unused interrupts during initialization.
