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

In TF-A, when the Boot ROM finishes and hands over to our firmware, or if we bypass ROM and start TF-A directly, the entry point is defined in assembly in `bl1/aarch64/bl1_entrypoint.S`.

Let's look at the actual `bl1_entrypoint` source:

```asm
func bl1_entrypoint
    /* ---------------------------------------------------------------------
     * If the reset address is programmable then bl1_entrypoint() is
     * executed only on the cold boot path. Therefore, we can skip the warm
     * boot mailbox mechanism.
     * ---------------------------------------------------------------------
     */
    el3_entrypoint_common                               \
        _init_sctlr=1                                   \
        _warm_boot_mailbox=!PROGRAMMABLE_RESET_ADDRESS  \
        _secondary_cold_boot=!COLD_BOOT_SINGLE_CPU      \
        _init_memory=1                                  \
        _init_c_runtime=1                               \
        _exception_vectors=bl1_exceptions              \
        _pie_fixup_size=0

    /* --------------------------------------------------------------------
     * Jump to C entry point
     * --------------------------------------------------------------------
     */
    bl  bl1_main

    /* --------------------------------------------------
     * Transition to the next boot image.
     * --------------------------------------------------
     */
#if BL2_RUNS_AT_EL3
    b   bl1_run_bl2_in_el3
#else
    b   el3_exit
#endif
endfunc bl1_entrypoint
```

Two things stand out immediately:

1. After `el3_entrypoint_common` expands and does all the heavy lifting, the entrypoint calls `bl bl1_main` directly — no intermediate setup function. `bl1_main` is the single C entry point.

2. `bl1_main` never returns in the traditional sense. When it's done, the assembly branches to either `bl1_run_bl2_in_el3` (when BL2 runs at EL3, e.g. with `RESET_TO_BL2`) or `el3_exit` (the standard path where BL2 runs at S-EL1). We covered both paths in Day 3.

## Cold Boot vs Warm Boot

Before diving into `el3_entrypoint_common`, look at the comment regarding the **warm boot mailbox** parameter. What does that mean?

A **cold boot** (or Power-On Reset) happens when you first turn the system on — the CPU has no state. A **warm boot** happens when a CPU wakes up from a low-power state (like PSCI `CPU_SUSPEND` or `CPU_ON` for a secondary core) where the core lost its architectural state but some persistent memory like Trusted SRAM is still valid.

If an SoC has a *programmable* reset address (meaning a power controller can be told "when you wake up this CPU, start execution at address X"), then warm boots will jump directly to a specialized resume handler, skipping `bl1_entrypoint()` entirely. This is the `PROGRAMMABLE_RESET_ADDRESS=1` case — the hardware itself routes the warm boot to the right place.

However, if the reset vector is *fixed* (not programmable), both cold and warm boots jump back to the same `bl1_entrypoint`. To differentiate between the two, software uses a **mailbox** — a designated location in persistent Trusted SRAM where an entry point address was written before the core went to sleep. The macro calls `plat_get_my_entrypoint()`, which queries a platform-specific source (e.g., the power controller's status register on FVP). If the returned address is non-zero, this is a warm boot and the code branches directly to that address. If it returns zero, this is a cold boot and initialization proceeds:

```asm
/* Warm boot detection inside _warm_boot_mailbox */
bl   plat_get_my_entrypoint
cbz  x0, do_cold_boot   /* zero = cold boot, continue init */
br   x0                 /* non-zero = warm boot, jump to resume handler */

do_cold_boot:
    /* ... cold boot initialization follows ... */
```

## The `el3_entrypoint_common` Macro

You'll notice the entrypoint uses a large macro `el3_entrypoint_common` (defined in `include/arch/aarch64/el3_common_macros.S`).

Why is it called `common`? Because these low-level architectural initialization steps aren't unique to BL1. Whether the CPU is booting cold (BL1), entering the runtime firmware (BL31), or waking up via PSCI `CPU_ON`, it finds itself at EL3 and needs a safe, known environment. TF-A uses this heavily parameterized macro to avoid duplicating the same raw setup code across all these stages. Each caller passes different parameter combinations to enable or disable the steps it needs.

Here is the exact order of operations the macro performs for BL1:

1. `_init_sctlr` — Write a known-good `SCTLR_EL3` value
2. `_warm_boot_mailbox` — Detect cold vs warm boot and branch if warm
3. Set `VBAR_EL3` to the BL1 exception vector table
4. `call_reset_handler` — Apply CPU-specific errata workarounds
5. `el3_arch_init_common` — Initialize `SCR_EL3`, `MDCR_EL3`, `CPTR_EL3`, enable I-cache
6. `setup_el3_execution_context` — Set up EL3 execution context registers
7. `_secondary_cold_boot` — Park secondary CPUs
8. `_init_memory` — Call `platform_mem_init()`
9. `_init_c_runtime` — Zero `.bss`, copy `.data` from ROM to RAM
10. Switch to `SP_EL0` and call `plat_set_my_stack`

Let's walk through the important ones.

## Architectural Initialization

Before you can even think about running C code, the CPU is in a very raw state. Wait, isn't the default architectural state of an ARM CPU on reset to have the MMU and caches already off? Yes, it is!

However, we explicitly reset the control registers to a known value anyway for defensive programming. Some SoCs have intermediate vendor ROMs that run before TF-A, and they might leave caches enabled or leave the CPU in a dirty state. Furthermore, during a soft reset triggered by a watchdog or a crash, the CPU might jump back to the entry point without a full hardware reset. Writing fresh, explicit values ensures we always start from a known, deterministic, and safe state.

### Step 1: Writing a Safe `SCTLR_EL3`

The System Control Register for EL3 (`SCTLR_EL3`) controls fundamental architectural features: the MMU, data cache, endianness, alignment checking, and more.

Here's the key insight about the actual TF-A code: it does **not** read the current `SCTLR_EL3` value and clear individual bits. Instead, it writes a **completely fresh value** computed from a known reset constant:

```asm
/* From el3_common_macros.S - _init_sctlr block */
mov_imm  x0, (SCTLR_RESET_VAL & ~(SCTLR_EE_BIT | SCTLR_WXN_BIT \
                | SCTLR_SA_BIT | SCTLR_A_BIT | SCTLR_DSSBS_BIT))
#if ENABLE_FEAT_RAS
    orr  x0, x0, #SCTLR_IESB_BIT
#endif
msr  sctlr_el3, x0
isb
```

The comment in the source explains why: *"Some fields reset to an IMPLEMENTATION DEFINED value and others are architecturally UNKNOWN on reset."* You simply cannot trust what `mrs sctlr_el3, x0` would return. So TF-A starts from `SCTLR_RESET_VAL` (a compile-time constant with safe defaults) and surgically clears a specific set of bits:

- **`SCTLR_EE_BIT`**: Force Little-Endian data accesses. Must be set before any memory access.
- **`SCTLR_WXN_BIT`**: Don't force writeable regions to be Execute-Never. Cleared so TF-A can execute from SRAM during early boot.
- **`SCTLR_SA_BIT` / `SCTLR_A_BIT`**: Disable stack and general alignment checks. Not needed yet.
- **`SCTLR_DSSBS_BIT`**: Disable speculation store bypass behavior on exception entry.

Notice what is *not* cleared here: `SCTLR_M_BIT` (MMU), `SCTLR_C_BIT` (D-cache), and `SCTLR_I_BIT` (I-cache). That's because `SCTLR_RESET_VAL` already has them cleared by definition. The MMU and D-cache stay off — but the I-cache gets enabled *shortly after*, as we'll see.

### Step 2: Exception Vectors

Even this early in boot, things can go wrong — an asynchronous abort, a bad memory access. TF-A immediately installs a basic exception vector table for BL1 by writing to `VBAR_EL3`:

```asm
adr   x0, bl1_exceptions
msr   vbar_el3, x0
isb
```

This happens **before** the secondary core check, so all cores — primary and secondary — have valid exception handlers from the very first moment.

### Step 3: CPU Errata Workarounds (`call_reset_handler`)

Right after setting the exception vectors, the macro calls `call_reset_handler`. This expands into two calls:

1. `plat_reset_handler()` — a platform-specific hook for very early SoC setup
2. The CPU-specific reset handler from the `cpu_ops` table — this is where **CPU errata workarounds** are applied

ARM CPUs have silicon bugs (errata) that are fixed via software workarounds applied immediately at reset. TF-A maintains a database of these in `lib/cpus/aarch64/` — for example, `cortex_a55.S`, `cortex_a76.S` etc. The CPU-specific handler reads the MIDR (Main ID Register) to identify the exact CPU revision and applies the appropriate register patches. This must happen as early as possible, before any of the buggy behavior can be triggered.

### Step 4: `el3_arch_init_common` — More Than Meets the Eye

After errata workarounds, `el3_arch_init_common` initializes a cluster of critical EL3 system registers. The important things it does:

**Enables the instruction cache:**
```asm
mov_imm  x1, (SCTLR_I_BIT | SCTLR_A_BIT | SCTLR_SA_BIT)
mrs      x0, sctlr_el3
orr      x0, x0, x1
msr      sctlr_el3, x0
isb
```

Yes — the I-cache is **enabled** at this point, well before `bl1_main()` is called. Instructions can now be fetched from a cache rather than going to ROM on every fetch. The data cache and MMU remain off.

**Initializes `SCR_EL3` from scratch:**
The Secure Configuration Register is `ARCHITECTURALLY UNKNOWN` on reset (the architecture doesn't guarantee its value). TF-A writes a known reset value (`SCR_RESET_VAL`) rather than relying on hardware defaults. Key bits set include: `SCR_RW_BIT` (next EL runs AArch64), and others.

**Initializes `MDCR_EL3` and `CPTR_EL3`:**
- `MDCR_EL3` controls debug and performance monitor access from lower ELs — initialized to safe defaults.
- `CPTR_EL3` controls architectural feature traps (floating-point, SVE, SME) — set so BL1 can use floating-point if needed.

### Step 5: Secondary Cores

Modern SoCs have multiple cores. On many systems, all cores are released from reset simultaneously and all start executing `bl1_entrypoint()`. We only want *one* primary core (the boot CPU) to do the early boot initialization to prevent race conditions.

After `el3_arch_init_common` and `setup_el3_execution_context`, the macro checks for secondary cores:

```asm
/* From el3_entrypoint_common - _secondary_cold_boot block */
.if _secondary_cold_boot
    bl    plat_is_my_cpu_primary
    cbnz  w0, do_primary_cold_boot

    /* This is a cold boot on a secondary CPU */
    bl    plat_secondary_cold_boot_setup
    /* plat_secondary_cold_boot_setup() is not supposed to return */
    bl    el3_panic

do_primary_cold_boot:
.endif
```

How does `plat_is_my_cpu_primary` work? On FVP (`plat/arm/board/fvp/aarch64/fvp_helpers.S`), it reads `MPIDR_EL1` (the Multiprocessor Affinity Register), masks off the affinity fields, and compares against a compile-time constant `FVP_PRIMARY_CPU`. If they match, the function returns 1 (primary); otherwise 0 (secondary). The key point is that a *specific predefined affinity value* designates the primary — not simply "core 0".

The secondary cores jump into `plat_secondary_cold_boot_setup`. Why is this a *platform-specific* function? Because how a core is safely parked depends heavily on the SoC's power management hardware:
- **FVP**: Interacts with the Base Power Controller (`PPOFFR` register) to power off the core, then executes `wfi`. The core will be re-powered later via PSCI `CPU_ON`.
- **Raspberry Pi**: Polls a specific hardware mailbox address, spinning on `wfe` until the primary writes an entry point there.

Regardless of *how* the platform implements it, secondary cores stay parked until the primary explicitly wakes them via PSCI (covered in a later article). Because this setup function is not supposed to return, if it ever does, TF-A triggers `el3_panic`.

### Step 6: Memory Initialization (`platform_mem_init`)

Once only the primary core is running, the `_init_memory` block calls `platform_mem_init()`. This is a platform hook for any early memory subsystem initialization needed before the C runtime can be set up — for example, initializing SRAM controllers or setting up memory ECC. Only the primary core reaches this point.

### Step 7: C Runtime Setup

Now the primary core sets up the C runtime environment. This has three sub-steps:

**Zero the `.bss` section:**
Uninitialized global variables in C are guaranteed to be zero by the C standard. The compiler places them in `.bss` but doesn't embed zero bytes in the binary (that would bloat ROM). The runtime must zero this region before any C code runs:

```asm
adrp  x0, __BSS_START__
add   x0, x0, :lo12:__BSS_START__
adrp  x1, __BSS_END__
add   x1, x1, :lo12:__BSS_END__
sub   x1, x1, x0
bl    zeromem
```

This is done **only by the primary core**. If secondary cores also zeroed `.bss`, they could wipe out data the primary had already initialized. Stack setup, however, must be done by every core independently.

**Copy `.data` from ROM to RAM (BL1-specific):**
This step is easy to overlook but is critical for BL1. BL1 executes from ROM, but initialized global variables (`int foo = 42;`) can't live in ROM — they need to be writable in RAM. The linker script places a ROM copy of the `.data` section alongside the code, and `el3_entrypoint_common` copies it into RAM using `memcpy16`:

```asm
/* Only for IMAGE_BL1 */
adrp  x0, __DATA_RAM_START__   /* destination: RAM */
adrp  x1, __DATA_ROM_START__   /* source: ROM copy */
adrp  x2, __DATA_RAM_END__
sub   x2, x2, x0               /* size */
bl    memcpy16
```

Without this step, every initialized global variable would contain garbage — whatever happens to be in SRAM at power-on.

### Step 8: Stack Setup

Finally, the stack. This is subtler than it looks. The C runtime in TF-A uses `SP_EL0` (not `SP_EL3`) for the stack:

```asm
/* Switch the active stack pointer to SP_EL0 */
msr  spsel, #0

/* Let the platform compute and set the per-CPU stack address */
bl   plat_set_my_stack
```

**Why `SP_EL0` and not `SP_EL3`?**

EL3 maintains two stack pointers: `SP_EL3` (used when an exception is taken to EL3) and `SP_EL0` (the general-purpose runtime stack). TF-A deliberately uses `SP_EL0` for all C code so that the exception-level-specific `SP_EL3` remains untouched and ready for exception handlers. If TF-A used `SP_EL3` for C code, an exception would overwrite the running stack — a catastrophic bug.

`plat_set_my_stack` computes the correct stack address for each CPU based on its index (from `plat_my_core_pos()`). Each CPU gets its own dedicated stack region in Trusted SRAM.

---

With the MMU off (so we are using physical addresses directly), the D-cache off, the I-cache on, the `.bss` zeroed, the `.data` copied from ROM to RAM, and the stack ready, the assembly code executes `bl bl1_main` and we finally enter the world of C!

I will see you in the next article in our series, where we will explore what `bl1_main` actually does: configuring early memory and loading BL2!
