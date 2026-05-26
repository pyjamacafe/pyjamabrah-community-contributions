---
date: "2026-05-25"
title: 'ARM TF-A Day 2: BL1 Deep Dive - The First Breath'
thumbnail: "/posts/arm-tfa-day2-bl1-reset/thumbnail.png"
author: "Mbharmal"
product: "embedded-engineering"
tags:
  - "ARM64"
  - "TF-A"
  - "BL1"
  - "Bootloader"
categories:
  - "arm-tfa"
---

In Day 1, we established that TF-A operates as the gatekeeper of the system, and that the boot process is a chain of trust broken into several stages. Today, I want to show you the very beginning of that chain: **BL1, the AP Trusted ROM**.

When you press the power button on an ARM SoC (or type `system_reset` in QEMU), the CPU comes out of reset. Where does it go? What's the very first instruction it executes?

<!--more-->

## The Reset Vector

When an ARMv8-A processor resets, the architecture dictates that execution begins at the highest implemented Exception Level. In a system with TrustZone (which is our focus), that means **EL3**. 

The hardware fetches its first instruction from a predetermined, hardcoded (well, almost true! This might change from design to design as we will see below) physical address known as the reset vector. 

Is it just a software address? No, the reset vector is essentially baked into the hardware! In the ARMv8 architecture, this address is determined by the `RVBAR_EL3` (Reset Vector Base Address Register for EL3) register. From the core's perspective, `RVBAR_EL3` is a **read-only** register; its value cannot be changed directly by a CPU instruction at runtime. 

However, SoC designers hardwire it by tying off physical pins (like the `RVBAREL3` input signals) on the CPU itself. In many modern designs, the values driving these pins are actually sourced from SoC-level configuration registers (often residing outside CPU's power domain). This means you *can* programmatically change the reset address by writing to these SoC registers, but the new value will only be sampled by the core on the *next* cold boot or reset!

By default, this address points to the start of the **Boot ROM** (or Trusted ROM), which is an immutable memory region programmed at the factory. For instance:
- **NXP i.MX8**: `RVBAR_EL3` is typically set to `0x00100000` (the start of the Boot ROM).
- **Raspberry Pi 5 (BCM2712)**: Points to an internal ROM address where the first stage bootloader sits.
- **STM32MP1**: `RVBAR_EL3` might be set to `0x00000000` mapped to the system memory alias of the ROM.

In TF-A, when the Boot ROM finishes and hands over to our firmware, or if we bypass ROM and start TF-A directly, the entry point is defined in assembly, typically in a file like `bl1/aarch64/bl1_entrypoint.S`.

Let's look at a snippet of `bl1_entrypoint`:

```asm
func bl1_entrypoint
    /* ---------------------------------------------------------------------
     * If the reset address is programmable then bl1_entrypoint() is
     * executed only on the cold boot path. Therefore, we can skip the warm
     * boot mailbox mechanism.
     * ---------------------------------------------------------------------
     */
    el3_entrypoint_common					\
        _init_sctlr=1					\
        _warm_boot_mailbox=!PROGRAMMABLE_RESET_ADDRESS	\
        _secondary_cold_boot=!COLD_BOOT_SINGLE_CPU	\
        _init_memory=1					\
        _init_c_runtime=1				\
        _exception_vectors=bl1_exceptions		\
        _pie_fixup_size=0
    
    /* --------------------------------------------------------------------
     * Perform BL1 setup
     * --------------------------------------------------------------------
     */
    bl	bl1_setup

    /* --------------------------------------------------------------------
     * Initialize the C runtime environment
     * --------------------------------------------------------------------
     */
    bl	bl1_main
endfunc bl1_entrypoint
```

Before we talk about the macro, look at the comment regarding the **warm boot mailbox**. What does that mean? 
A **cold boot** (or Power-On Reset) happens when you first turn the system on. A **warm boot** happens when a CPU wakes up from a deep sleep (like Suspend-to-RAM) where it lost its state but power wasn't fully severed from the system.
If an SoC has a *programmable* reset address (meaning a power controller can be told "when you wake up the CPU, start execution at address X"), then warm boots will jump directly to a specialized resume handler, skipping `bl1_entrypoint()`. However, if the reset vector is *fixed* (not programmable), both cold and warm boots jump back to this very same `bl1_entrypoint()`. To differentiate between the two, software checks a "mailbox"—a designated location in persistent SRAM where a flag was written before going to sleep. If the flag is set, it's a warm boot!

You'll notice it uses a massive macro `el3_entrypoint_common`. (For those following along in the source tree, you can find the full path to this macro at `include/arch/aarch64/el3_common_macros.S`).

Why is it called `common`? Because these low-level architectural initialization steps aren't unique to BL1. Whether the CPU is booting from a cold start (BL1), jumping to the runtime firmware (BL31), or waking up from a deep sleep state via PSCI CPU_ON, it finds itself in EL3 and needs a safe, known environment. TF-A uses this heavily parameterized macro to avoid duplicating the same raw setup code across all these stages.

Let's zoom into a couple of snippets from `el3_entrypoint_common` to see what it's actually doing. It handles the heavy lifting of raw architectural initialization. 

## Architectural Initialization

Before you can even think about running C code (like `bl1_main()`), the CPU is in a very raw state. Wait, isn't the default architectural state of an ARM CPU on reset to have the MMU and caches already off? Yes, it is! 

However, we explicitly turn them off in code anyway for defensive programming. Some SoCs have intermediate vendor ROMs that run before TF-A, and they might leave caches enabled or leave the CPU in a dirty state. Furthermore, during a soft reset triggered by a watchdog or a crash, the CPU might jump back to the entry point without a full hardware reset. Explicitly disabling the MMU and caches ensures we always start from a known, deterministic, and safe state. Branch prediction is off, and we don't even have a stack!

The `el3_entrypoint_common` macro handles these critical first steps:

### 1. The SCTLR_EL3 Register
The System Control Register for EL3 (`SCTLR_EL3`) controls the fundamental architectural features. Early in the boot process, TF-A ensures that the Instruction Cache (I-Cache) is disabled, the Data Cache (D-Cache) is disabled, and the MMU is turned off. 

Why? Because RAM hasn't been initialized yet, and the translation tables don't exist! If the MMU were on, the CPU would immediately fault trying to translate virtual addresses.

```asm
/* Snippet from el3_entrypoint_common */
mrs	x0, sctlr_el3
bic	x0, x0, #SCTLR_EE_BIT   /* Clear Endianness bit (Little Endian) */
bic	x0, x0, #SCTLR_M_BIT    /* Disable MMU */
bic	x0, x0, #SCTLR_C_BIT    /* Disable D-Cache */
bic	x0, x0, #SCTLR_I_BIT    /* Disable I-Cache */
msr	sctlr_el3, x0
isb
```

### 2. Secondary Cores
Modern SoCs have multiple cores. Do they all boot out of reset at once, or just a designated boot core? This depends on the SoC hardware. On many systems, all cores are released from reset simultaneously. 

If all cores wake up at once, they will all start executing `bl1_entrypoint()`. However, we only want *one* primary core (usually CPU0) to do the early boot initialization (setting up memory, loading BL2) to prevent catastrophic race conditions. 

How do the cores know who they are? TF-A uses the `plat_is_my_cpu_primary` function (which internally reads `MPIDR_EL1` to get the core ID) to determine if it's the chosen one. 

If a core realizes it is the primary core, it proceeds to initialize the system. If it is a secondary core, it calls a platform-specific setup function which parks the core in a holding pen. Inside `el3_entrypoint_common`, you will find the exact logic:

```asm
    /* Snippet from el3_entrypoint_common */
    .if \_secondary_cold_boot
    bl  plat_is_my_cpu_primary
    cbnz w0, do_primary_cold_boot

    /* This is a cold boot on a secondary CPU */
    bl  plat_secondary_cold_boot_setup
    /* plat_secondary_cold_boot_setup() is not supposed to return */
    bl  el3_panic
    .endif /* _secondary_cold_boot */
```
The secondary cores jump into `plat_secondary_cold_boot_setup`. Why is this a *platform-specific* function? Because how a core is safely parked heavily depends on the SoC's power management hardware. Some platforms might just spin in a simple Wait For Interrupt (`wfi`) loop, while others might need to interact with a specific power controller to put the core into a deep low-power state, or poll a proprietary hardware mailbox register.

If you browse the TF-A source, you can see how different platforms implement this:
- **QEMU**: Often implements a basic `wfi` loop in `plat/qemu/qemu/aarch64/plat_helpers.S`.
- **Raspberry Pi**: Implements its own holding pen logic, often polling a specific mail box address in `plat/rpi/common/aarch64/plat_helpers.S`.

Regardless of *how* the platform implements it, the secondary cores stay parked until they are explicitly woken up later by the primary core (which I will cover in the PSCI chapters!). Because this setup function is not supposed to return, if it ever does, TF-A triggers a critical `el3_panic`!

### 3. Setting up the Exception Vectors
Even this early in the boot, things can go wrong (like an asynchronous abort). TF-A sets up a rudimentary exception vector table specifically for BL1 by writing the base address to the `VBAR_EL3` register.

```asm
adr	x0, bl1_exceptions
msr	vbar_el3, x0
isb
```

### 4. Establishing the C Runtime
Finally, before we jump into `bl1_main()`, we need a stack. The assembly code sets the Stack Pointer (`sp_el3`) to a designated piece of secure SRAM. 

It is important to note a clear separation here: **Stack setup** has to be done by *every* core when it runs (because every core needs a stack to execute C code). Remember, depending upon the **boot-flow architecture** for a particular SoC, we might or might not see the secondary cores setup their stack because they might not be executing in the C context at all (again, depending on the Boot flow).

However, **zeroing out the `.bss` section** (uninitialized variables) must only be done *once* by the primary core to prevent secondary cores from wiping out data that has already been initialized! 

```asm
/* Snippet: Set the stack pointer */
adrp    x0, __DATA_RAM_END__
add     x0, x0, :lo12:__DATA_RAM_END__
mov     sp, x0
```

With the MMU off (so we are using physical addresses directly), the caches off, the stack ready, and the `.bss` cleared by the primary core, the assembly code executes `bl bl1_main` and we finally enter the world of C!

I will see you in the next article in our series, where we will explore what `bl1_main` actually does: configuring early memory and loading BL2!
