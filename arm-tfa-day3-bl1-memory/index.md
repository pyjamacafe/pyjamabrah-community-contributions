---
date: "2026-05-26"
title: 'ARM TF-A Day 3: BL1 Deep Dive - Memory Management and Handoff'
thumbnail: "/posts/arm-tfa-day3-bl1-memory/thumbnail.png"
author: "Mbharmal"
product: "embedded-engineering"
tags:
  - "ARM64"
  - "TF-A"
  - "BL1"
  - "MMU"
categories:
  - "arm-tfa"
---

Hello! To recap in Day 2's article, we covered the primary CPU core wake up, disable its caches, set up a stack pointer, and finally jump into `bl1_main()`. 

Today, we'll walk you through what BL1 actually does in C, setting up early memory management and handling the crucial task of loading the next stage (BL2) from flash into SRAM.

<!--more-->

## The `bl1_main()` Function

When you drop into `bl1_main()` (located in `bl1/bl1_main.c`), the CPU is still running purely on physical addresses. To run more complex C code efficiently, you need the MMU and data caches enabled. 

*(Wait, why is the MMU strictly required if ARMv8-A data caches are PIPT (Physically Indexed, Physically Tagged)? It's because the ARM architecture dictates that memory attributes—like Cacheability—are defined by the translation tables in the MMU. When the MMU is disabled, the CPU treats all data memory accesses as Non-cacheable (or Device memory), completely bypassing the cache. Thus, the MMU must be enabled to utilize the data cache!)* 

```c
void bl1_main(void)
{
	/* Perform remaining generic architectural setup from EL3 */
	bl1_arch_setup();

	/* Perform platform setup in BL1 */
	bl1_platform_setup();

	/* Enable the MMU and data cache */
	bl1_plat_arch_setup();

	/* Load the next image (BL2) */
	bl1_load_bl2();

	/* Handoff to BL2 */
	bl1_prepare_next_image(BL2_IMAGE_ID);
}
```

## Early Memory Management (Translation Tables)

Before `bl1_plat_arch_setup()` enables the MMU, TF-A must construct the translation tables (Page Tables). Because BL1 is tiny and runs out of ROM and small chunks of SRAM, it doesn't need to map the entire gigabytes of system DRAM. It only maps what it needs to survive:

1.  **The ROM region** (where BL1's code is executing) mapped as Read-Only, Executable.
2.  **The Secure SRAM region** (where BL1's data and stack live) mapped as Read-Write, Execute-Never (XN) for security.
3.  **Basic Peripheral Memory** (like the UART base address for early console debugging) mapped as Device Memory.

TF-A uses its `xlat_tables` library to build these level 1, 2, and 3 page table entries dynamically based on platform-specific `#define`s. Once the tables are built in SRAM, `SCTLR_EL3.M` is flipped to `1`, and the MMU is alive!

## The Firmware Image Package (FIP)

Now that we have memory, BL1 needs to find BL2. But BL2 isn't just sitting raw on the flash memory. TF-A uses a container format called the **Firmware Image Package (FIP)**.

Think of a FIP as a mini-filesystem or a `.tar` archive. It contains BL2, BL31, BL32, and BL33, all packed together. Each image inside the FIP is identified by a unique UUID. *(We will dive deeper into the structure and usage of FIPs in a later article).*

BL1's platform-specific code knows exactly where the FIP starts in the non-volatile storage (e.g., eMMC, NOR Flash, or QEMU's emulated flash). To read from this storage, BL1 (the BootROM) must contain the specific device drivers. 

**A BootROM Design Tradeoff:**
Depending on the board design, you might want the SoC to boot from SD card, eMMC, or SPI NOR. To support all these, the BootROM would need drivers for *all* of them. However, more drivers mean a larger BootROM size. Since BootROM is immutable (burned into silicon), a larger codebase directly increases the attack surface and the probability of latent, unpatchable bugs. SoC designers must carefully balance boot flexibility against security and size!

## Loading BL2

The function `bl1_load_bl2()` uses the TF-A IO storage framework to read the FIP from flash.

1.  It searches the FIP header for the UUID corresponding to `BL2_IMAGE_ID`.
2.  Once found, it reads the BL2 binary into a specific, predetermined location in Secure SRAM. (Why SRAM and not DRAM? Because DRAM hasn't been initialized yet! That's BL2's job).
3.  During this load, if Trusted Board Boot (TBBR) is enabled, BL1 will cryptographically verify the image. The "golden hash" (specifically the hash of the Root of Trust Public Key, or ROTPK) is stored immutably in the SoC's eFuses or OTP memory. TF-A performs this verification via the `load_auth_image(BL2_IMAGE_ID, info)` function called within `bl1_main.c`.

## The Handoff

With BL2 safely sitting in Secure SRAM, BL1's job is done. It's time to pass the baton.

Because BL1 and BL2 are completely separate firmware stages—compiled separately and running at different exception levels—there is no standard C function calling convention (ABI) between them. 

To bridge this gap, BL1 populates an `entry_point_info_t` structure. This struct acts as the agreed-upon handoff mechanism, containing the entry point address of BL2, the `SPSR_EL3` (Saved Program Status Register) value that dictates the execution state BL2 should run in, and an `args` array (`arg0` through `arg7`). Platform implementations can use these `args` to pass vendor-specific custom data down to BL2!

The final act of BL1 happens back in assembly:

```asm
func el3_exit
    /* ... restores registers ... */
    
    /* 
     * The `eret` instruction causes the CPU to take the address 
     * from ELR_EL3 and jump to it, while simultaneously 
     * changing the exception level based on SPSR_EL3!
     */
    eret
endfunc el3_exit
```

The `eret` (Exception Return) instruction fires. The CPU blindly teleports to the BL2 entry point in Secure SRAM and drops its privilege from EL3 down to Secure-EL1 (or S-EL2). 

**How does the CPU know which state to enter?**
When `eret` executes, the CPU looks directly at the `SPSR_EL3` (Saved Program Status Register) to determine the target Exception Level (e.g., EL1) and execution state (AArch64 vs AArch32). To determine whether this new state is in the Secure or Non-Secure world, the CPU checks the `NS` (Non-Secure) bit inside the `SCR_EL3` (Secure Configuration Register), *not* `SCTLR_EL3` as one might intuitively guess! If `SCR_EL3.NS = 0`, the CPU drops into Secure-EL1.

**Exercise**: Those curious among us are given a small task to find out this piece of code which does this transition from EL3 to S-EL1 or S-EL2.

*(Note: While ARM recommends BL2 runs at S-EL1, some legacy or specific SoC boot architectures might configure BL2 to run at EL3 instead).*

**What about the Vector Table?**
Notice that BL1 *did not* set up `VBAR_EL1` (the Vector Base Address Register) for BL2. BL1 only knows the raw entry point address. As soon as BL2 starts executing (typically in its `bl2_entrypoint` assembly code), its very first responsibility is to initialize its own environment, which includes setting `VBAR_EL1` to point to its own vector table.

With the `eret` complete, BL1 is officially history.

I will see you in Day 4, where we will explore what BL2 does now that it has the control!
