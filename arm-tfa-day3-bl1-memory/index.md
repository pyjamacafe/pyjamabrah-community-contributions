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

Here is an annotated, simplified view of the actual `bl1_main()` flow. Notice the `__no_pauth` attribute — just like BL2 (as we will see in the next article), this function runs before Pointer Authentication is configured:

```c
void __no_pauth bl1_main(void)
{
    unsigned int image_id;

    /* 1. Enable early console (if EARLY_CONSOLE flag set) */
    plat_setup_early_console();

    /* 2. Early platform setup: console, SRAM layout, watchdog */
    bl1_early_platform_setup();

    /* 3. Platform arch setup: build page tables, enable MMU */
    bl1_plat_arch_setup();

    /* 4. Initialize EL3 extension registers */
    cm_manage_extensions_el3(plat_my_core_pos());

    /* 5. Print version banners */
    NOTICE(FIRMWARE_WELCOME_STR);
    NOTICE("BL1: %s\n", build_version_string);
    NOTICE("BL1: %s\n", build_message);

    /* 6. Architectural setup: set SCR_EL3.RW for AArch64 */
    bl1_arch_setup();

    /* 7. Initialize crypto and authentication modules */
    crypto_mod_init();
    auth_mod_init();

    /* 8. Initialize measured boot */
    bl1_plat_mboot_init();

    /* 9. Platform setup: IO drivers, firmware config, timer */
    bl1_platform_setup();

    /* 10. Determine which image to load next */
    image_id = bl1_plat_get_next_image_id();

    /* 11. Load BL2 (or start firmware update) */
    if (image_id == BL2_IMAGE_ID)
        bl1_load_bl2();
    else
        NOTICE("BL1-FWU: *******FWU Process Started*******\n");

    /* 12. Teardown measured boot and crypto */
    bl1_plat_mboot_finish();
    crypto_mod_finish();

    /* 13. Prepare handoff context for next image */
    bl1_prepare_next_image(image_id);

    console_flush();
}
```

There are several things that jump out compared to what you might expect:

- **The MMU is enabled very early** — `bl1_plat_arch_setup()` is the second substantive call. After this, the data cache is on and C code runs at full speed. Everything after this point benefits from cached SRAM access.

- **`bl1_arch_setup()` is called AFTER the MMU is up**. It's a tiny one-liner that sets `SCR_EL3.RW` to ensure BL2 will execute in AArch64 mode. It has nothing to do with the MMU.

- **BL2 loading is conditional**. BL1 asks the platform for the next image ID via `bl1_plat_get_next_image_id()`. Normally this returns `BL2_IMAGE_ID`, but if the FIP is corrupt or a firmware update is needed, it returns `NS_BL1U_IMAGE_ID` and BL1 enters the Firmware Update (FWU) path instead.

- **Crypto and authentication are initialized before images are loaded**. The `crypto_mod_init()` and `auth_mod_init()` calls prepare the chain of trust so that `load_auth_image()` can verify BL2's signature during the load process.

## Early Platform Setup

The very first substantial call in `bl1_main()` is `bl1_early_platform_setup()`. On ARM reference platforms (in `plat/arm/common/arm_bl1_setup.c`), this function does critical bootstrapping work:

1.  **Starts the secure watchdog** (`plat_arm_secure_wdt_start()`). If BL1 hangs during boot, the watchdog will reset the system. It's disabled just before the handoff to BL2.

2.  **Initializes the boot console** (`arm_console_boot_init()`). This is the earliest point at which you'll see serial output. Without this call, all those `NOTICE()` prints would go nowhere.

3.  **Sets up the Trusted SRAM layout**. BL1 records the base and size of Trusted RAM (`ARM_BL_RAM_BASE`, `ARM_BL_RAM_SIZE`) so it can later determine where BL2 should be loaded.

4.  **Initializes the Transfer List** (on newer platforms). The Transfer List is a modern firmware handoff mechanism that replaces ad-hoc register passing. BL1 creates and initializes this structure so it can pass structured data to BL2.

5.  **Enables cache coherency interconnect** (CCI/CCN). On multi-cluster SoCs, this ensures that the primary CPU's cluster can see coherent memory.

## Early Memory Management (Translation Tables)

After `bl1_early_platform_setup()`, the next call is `bl1_plat_arch_setup()` which enables the MMU. Before it can do that, TF-A must construct the translation tables (Page Tables). Because BL1 is tiny and runs out of ROM and small chunks of SRAM, it doesn't need to map the entire gigabytes of system DRAM. It only maps what it needs to survive:

1.  **The ROM region** (where BL1's code is executing) mapped as Read-Only, Executable (`MT_CODE`).
2.  **The Secure SRAM region** (where BL1's data and stack live) mapped as Read-Write, Execute-Never (`MT_MEMORY | MT_RW`).
3.  **Basic Peripheral Memory** (like the UART base address for early console debugging) mapped as Device Memory (`MT_DEVICE`).

On ARM reference platforms, you can see these mappings defined as macros in `plat/arm/common/arm_bl1_setup.c`:

```c
#define MAP_BL1_TOTAL   MAP_REGION_FLAT(             \
                            bl1_tzram_layout.total_base, \
                            bl1_tzram_layout.total_size, \
                            MT_MEMORY | MT_RW | EL3_PAS)

#define MAP_BL1_RO      MAP_REGION_FLAT(             \
                            BL_CODE_BASE,            \
                            BL1_CODE_END - BL_CODE_BASE, \
                            MT_CODE | EL3_PAS)
```

TF-A uses its `xlat_tables` library to build these level 1, 2, and 3 page table entries dynamically based on platform-specific `#define`s. Once the tables are built in SRAM, the function calls `enable_mmu_el3(0)` which flips `SCTLR_EL3.M` to `1`, and the MMU is alive!

After `bl1_plat_arch_setup()` returns, the code asserts (in debug builds) that the MMU, data cache, and instruction cache are all enabled — a safety check that platform code didn't leave the system in a broken state:

```c
#if ENABLE_ASSERTIONS
    val = read_sctlr_el3();
    assert((val & SCTLR_M_BIT) != 0);  /* MMU on */
    assert((val & SCTLR_C_BIT) != 0);  /* Data cache on */
    assert((val & SCTLR_I_BIT) != 0);  /* Instruction cache on */
#endif
```

## Platform Setup: IO, Config, and Timers

After the MMU is up, crypto is initialized, and authentication is ready, BL1 calls `bl1_platform_setup()`. On ARM reference platforms (in `plat/arm/common/arm_bl1_setup.c`), this function handles the remaining platform initialization:

1.  **IO layer initialization** (`plat_arm_io_setup()`). This registers IO device drivers — for NOR flash, eMMC, semihosting, etc. — and connects them to the IO storage framework. Without this, BL1 cannot read the FIP.

2.  **Firmware Update check** (`plat_arm_bl1_fwu_needed()`). The platform checks if the FIP's Table of Contents (ToC) is valid. If it's corrupt, the FWU path is triggered.

3.  **Firmware configuration loading**. BL1 loads `FW_CONFIG` and `TB_FW_CONFIG` device trees from the FIP. These configuration blobs describe the firmware topology (e.g., which SPD to use, where images should be loaded). On newer platforms using Transfer Lists, these are passed to BL2 via tagged entries.

4.  **System timer programming** (`write_cntfrq_el0(plat_get_syscnt_freq2())`). BL1 programs the system counter frequency register so that non-secure images and subsequent boot stages have access to a calibrated timer.

## The Firmware Image Package (FIP)

Now that we have memory and IO, BL1 needs to find BL2. But BL2 isn't just sitting raw on the flash memory. TF-A uses a container format called the **Firmware Image Package (FIP)**.

Think of a FIP as a mini-filesystem or a `.tar` archive. It contains BL2, BL31, BL32, and BL33, all packed together. Each image inside the FIP is identified by a unique UUID. *(We will dive deeper into the structure and usage of FIPs in a later article).*

BL1's platform-specific code knows exactly where the FIP starts in the non-volatile storage (e.g., eMMC, NOR Flash, or QEMU's emulated flash). To read from this storage, BL1 must contain the specific device drivers. 

**A BootROM Design Tradeoff:**
Depending on the board design, you might want the SoC to boot from SD card, eMMC, or SPI NOR. To support all these, the boot firmware would need drivers for *all* of them. However, more drivers mean a larger code size. If this code is in ROM (burned into silicon), a larger codebase directly increases the attack surface and the probability of latent, unpatchable bugs. SoC designers must carefully balance boot flexibility against security and size!

### Wait — Is BL1 Actually a BootROM?

Not necessarily, and this is an important distinction. On most real SoCs, the boot chain actually looks like this:

1.  **Silicon BootROM** (vendor-specific, burned into the die, completely immutable) — this is the true hardware root of trust. It's not part of TF-A. The vendor writes it, masks it into silicon, and it can never be updated. It typically does very little: initialize the minimum clocks, read bootstrap pins to determine boot media, load the first firmware image from storage, and optionally verify its signature against an OTP-fused key.

2.  **TF-A BL1** (may live in NOR flash, eMMC, or even SRAM loaded by the BootROM) — this is updatable firmware. The BootROM loads and verifies it, and then BL1 takes over.

3.  **TF-A BL2** (loaded by BL1 from the FIP into SRAM) — also updatable firmware.

So TF-A's BL1 is *not* the immutable silicon BootROM. On platforms like STM32MP1 or Raspberry Pi, the silicon BootROM is a separate, proprietary piece of code that has already executed before TF-A even enters the picture.

**If BL1 is updatable, why do we need BL2 at all?**

If both BL1 and BL2 are field-updatable firmware in flash, why not merge them and save an entire boot stage? The answer comes down to **privilege separation**:

-   **BL1 runs at EL3** (the highest privilege level). Its job is deliberately kept tiny: set up the MMU, verify and load BL2, and hand off. The less code running at EL3, the smaller the attack surface for the most privileged execution level.
-   **BL2 runs at S-EL1** (a lower privilege). It does the heavy lifting: DRAM initialization, FIP parsing, loading all remaining images (BL31, BL32, BL33). If there's a bug in the complex FIP parsing or DDR training code, it can only compromise S-EL1 — not the EL3 Secure Monitor.

This separation creates a **defense-in-depth** model. Even if an attacker finds a vulnerability in BL2's image parsing logic, they cannot directly take over EL3.

Additionally, BL1 and BL2 form a **chain of trust**: the silicon BootROM verifies BL1, then BL1 verifies BL2, then BL2 verifies everything else. Each link in the chain has a well-defined, minimal responsibility.

**But what if I don't need this separation?**

Then you can skip BL1 entirely! This is exactly what the `RESET_TO_BL2` build flag does (as we will see in the next article). When `RESET_TO_BL2=1`, BL2 becomes the reset vector and runs at EL3 — no BL1 at all. The silicon BootROM loads BL2 directly, and BL2 handles everything: MMU setup, DRAM init, image loading, and handoff to BL31.

Real platforms use this approach when:
-   The silicon BootROM already provides sufficient trust anchoring (e.g., STM32MP1, where the BootROM verifies the first TF-A image)
-   Boot time is critical and an extra stage adds unacceptable latency
-   The platform doesn't need the EL3/S-EL1 privilege split during boot

In short: **BL1 exists for platforms that want EL3 privilege separation during boot. For platforms that don't, `RESET_TO_BL2` eliminates it entirely.** The TF-A architecture gives SoC designers the flexibility to choose.

## Loading BL2

The function `bl1_load_bl2()` (in `bl1/bl1_main.c`) orchestrates the loading and authentication of BL2. Rather than directly searching FIP headers, it uses TF-A's layered architecture:

```c
static void bl1_load_bl2(void)
{
    image_desc_t *desc;
    image_info_t *info;
    int err;

    /* Get the image descriptor */
    desc = bl1_plat_get_image_desc(BL2_IMAGE_ID);

    info = &desc->image_info;
    INFO("BL1: Loading BL2\n");

    /* Platform pre-load hook */
    err = bl1_plat_handle_pre_image_load(BL2_IMAGE_ID);
    ...

    /* Load and authenticate the image */
    err = load_auth_image(BL2_IMAGE_ID, info);
    ...

    /* Platform post-load hook */
    err = bl1_plat_handle_post_image_load(BL2_IMAGE_ID);
    ...

    NOTICE("BL1: Booting BL2\n");
}
```

Here's what happens step by step:

1.  BL1 asks the platform for the BL2 image descriptor via `bl1_plat_get_image_desc()`. This descriptor contains the target load address in Secure SRAM. (Why SRAM and not DRAM? Because DRAM hasn't been initialized yet! That's BL2's job).

2.  The `bl1_plat_handle_pre_image_load()` hook lets the platform prepare for loading — for example, setting up DMA or adjusting memory permissions.

3.  `load_auth_image(BL2_IMAGE_ID, info)` is where the real magic happens. This function internally uses the IO storage framework (`drivers/io/`) to locate BL2 inside the FIP by its UUID, read it into SRAM, and if Trusted Board Boot (TBBR) is enabled, cryptographically verify its signature. The "golden hash" (specifically the hash of the Root of Trust Public Key, or ROTPK) is stored immutably in the SoC's eFuses or OTP memory.

4.  After loading, `bl1_plat_handle_post_image_load()` lets the platform do post-processing. On platforms using Transfer Lists, this hook adds the SRAM layout information as a tagged entry so BL2 can discover its available memory.

## The Handoff

With BL2 safely sitting in Secure SRAM, BL1's job is done. It's time to pass the baton.

Because BL1 and BL2 are completely separate firmware stages—compiled separately and potentially running at different exception levels—there is no standard C function calling convention (ABI) between them. 

### Preparing the Context

BL1 calls `bl1_prepare_next_image(image_id)` to set up the execution context for BL2. This function (in `bl1/aarch64/bl1_context_mgmt.c`) populates an `entry_point_info_t` structure containing:

-   The **entry point address** of BL2 (where execution will jump to)
-   The **`SPSR_EL3`** value — the Saved Program Status Register that dictates the target exception level and execution state
-   An **`args`** struct (`arg0` through `arg7`) for passing platform-specific data (e.g., the firmware config address or Transfer List pointer)

For the standard BL2-at-S-EL1 path, the function also calls `cm_init_my_context()` and `cm_prepare_el3_exit()` to initialize the full EL3 context management framework — setting up `SCR_EL3`, `ELR_EL3`, and other system registers for the transition.

### Two Exit Paths

The final act of BL1 happens back in assembly (in `bl1/aarch64/bl1_entrypoint.S`), right after `bl1_main()` returns:

```asm
    bl  bl1_main

#if BL2_RUNS_AT_EL3
    b   bl1_run_bl2_in_el3
#else
    b   el3_exit
#endif
```

There are two very different paths depending on whether BL2 runs at EL3:

**Path 1: Standard exit via `el3_exit` (BL2 at S-EL1)**

The `el3_exit` function (in `lib/el3_runtime/aarch64/context.S`) is far more than a simple `eret`. It's approximately 80 lines of assembly that:

1.  Saves the EL3 runtime stack pointer
2.  Restores `CPTR_EL3` from the per-world context
3.  Restores `SPSR_EL3`, `ELR_EL3`, `SCR_EL3`, and `MDCR_EL3`
4.  Restores all general-purpose registers and ARMv8.3-PAuth keys
5.  Executes `exception_return` — which is an `eret` followed by a **speculation barrier** (for Spectre-style mitigation):

```asm
.macro exception_return
    eret
    speculation_barrier   /* dsb sy + isb, or sb if FEAT_SB */
.endm
```

The `eret` instruction causes the CPU to take the address from `ELR_EL3` and jump to it, while simultaneously changing the exception level based on `SPSR_EL3`. The CPU drops its privilege from EL3 down to Secure-EL1.

**Path 2: Direct EL3→EL3 via `bl1_run_bl2_in_el3`**

When `BL2_RUNS_AT_EL3` is set (for `RESET_TO_BL2` or RME configurations), BL1 cannot use the context management framework because BL2 will replace BL1 entirely at EL3. Instead, it:

1.  Reads the `bl2_ep_info` entry point structure
2.  **Disables the MMU and instruction cache** (`bl disable_mmu_icache_el3`) — because BL2 at EL3 will set up its own address space
3.  Invalidates all TLBs (`tlbi alle3`)
4.  Loads `ELR_EL3` and `SPSR_EL3` from the entry point info
5.  Loads arguments `x0–x7` from the entry point args
6.  Executes `exception_return`

This is an **EL3→EL3 transition** — the processor stays at EL3 but jumps to BL2's entry point.

### How Does the CPU Know Which State to Enter?

When `eret` executes, the CPU looks at two registers:

-   **`SPSR_EL3`** determines the target Exception Level (EL1, EL2, etc.) and the execution state (AArch64 vs AArch32).
-   **`SCR_EL3`** determines the security state. The `NS` (Non-Secure) bit inside `SCR_EL3` (Secure Configuration Register) controls whether the target EL runs in the Secure or Non-Secure world. If `SCR_EL3.NS = 0`, the CPU drops into Secure-EL1.

**Exercise**: Try to find the code in `bl1_prepare_next_image()` (in `bl1/aarch64/bl1_context_mgmt.c`) that constructs the `SPSR_EL3` value. What mode does it use for Secure images? What about Non-Secure images?

**What about the Vector Table?**
Notice that BL1 *did not* set up `VBAR_EL1` or `VBAR_EL3` (the Vector Base Address Register) for BL2. BL1 only knows the raw entry point address. As soon as BL2 starts executing (typically in its `bl2_entrypoint` assembly code), its very first responsibility is to initialize its own environment, which includes setting `VBAR_EL1` or `VBAR_EL3` to point to its own vector table.

With the `eret` complete, BL1 is officially history.

I will see you in Day 4, where we will explore what BL2 does now that it has the control!
