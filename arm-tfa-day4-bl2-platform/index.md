---
date: "2026-05-31"
title: 'ARM TF-A Day 4: BL2 Deep Dive - Platform Initialization'
thumbnail: "/posts/arm-tfa-day4-bl2-platform/thumbnail.png"
author: "Mbharmal"
product: "embedded-engineering"
tags:
  - "ARM64"
  - "TF-A"
  - "BL2"
categories:
  - "arm-tfa"
---

With BL1 having done the heavy lifting of raw architectural setup, we have arrived at **BL2 (Trusted Boot Firmware)**. 

BL1 was executing out of ROM; BL2 is executing out of Secure SRAM. The environment is a little less hostile now, but we are still in the dark. It's time for BL2 to turn on the lights, initialize the broader platform, and prepare the system memory. Let's see how it's done.

<!--more-->

## Execution Context

Depending on the specific architectural configuration, BL2 executes at either **EL3** or **Secure-EL1 (S-EL1)**. In modern, highly-structured TF-A deployments using TrustZone, BL2 typically runs at S-EL1. 

Why S-EL1? Because we want to minimize the amount of code running at EL3 (the highest privilege level). BL2's main job is parsing complex FIP packages and setting up DRAM. If there is a bug or vulnerability in the FIP parsing logic, we'd rather it exploit S-EL1 than compromise the entire EL3 Secure Monitor. The typical boot flow for SoCs like standard FVP, Raspberry Pi, or STM32MP1 falls into this category.

However, there are scenarios where BL2 executes at EL3. In TF-A, this is controlled by the `RESET_TO_BL2 := 1` build flag. When this is enabled, BL2 acts as the reset handler and the system jumps directly to it without a BL1. You can find this in some Automotive Reference Designs (e.g., `plat/arm/board/automotive_rd/platform/rd1ae/platform.mk`), or older AArch32 systems lacking proper EL3 segregation.

Internally, the build system derives a separate flag `BL2_RUNS_AT_EL3` from this. In the main `Makefile`, you'll find:

```makefile
ifneq ($(filter 1 2,${RESET_TO_BL2} ${ENABLE_FEAT_RME}),)
    BL2_RUNS_AT_EL3 := 1
else
    BL2_RUNS_AT_EL3 := 0
endif
```

Notice that `BL2_RUNS_AT_EL3` is also set when `ENABLE_FEAT_RME` (Realm Management Extension) is enabled. This means that in a 4-world CCA (Confidential Compute Architecture) system, BL2 also runs at EL3 — even if `RESET_TO_BL2` is not set. You'll see `BL2_RUNS_AT_EL3` used extensively throughout the BL2 source to conditionally compile EL3-specific code paths.

## The Assembly Entry: `bl2_entrypoint`

The entry point for BL2 is `bl2_entrypoint`, defined in `bl2/aarch64/bl2_entrypoint.S`. Before any C code runs, the assembly stub performs several critical low-level tasks:

1.  **Save BL1's arguments**: BL1 passes information (like the Trusted SRAM layout and firmware config address) in registers `x0–x3`. The entrypoint immediately saves these into callee-saved registers (`x20–x23`) so they survive subsequent function calls.

2.  **Install exception vectors**: Sets `VBAR_EL1` to `early_exceptions`, giving BL2 a sane exception table in case something goes wrong early.

3.  **Enable the instruction cache and alignment checks**: Modifies `SCTLR_EL1` to turn on `I`, `A`, and `SA` bits. Note that the data cache and MMU are *not* enabled yet.

4.  **Invalidate the data cache for BL2's RW region**: BL1 loaded BL2's image into SRAM, but old dirty cache lines from a previous boot stage could cause corruption. The entrypoint calls `inv_dcache_range` on the entire `__RW_START__` to `__RW_END__` range.

5.  **Zero out .bss and coherent memory**: Calls `zeromem` on the BSS section. This is essential — C globals must start at zero.

6.  **Set up the stack**: Calls `plat_set_my_stack` to allocate a stack in SRAM.

7.  **Initialize stack protector canary** (if enabled): Before any C code runs.

Only after all of this does it restore the saved arguments into `x0–x3` and branch to `bl2_main`.

```asm
func bl2_entrypoint
    /* Save arguments x0-x3 from BL1 */
    mov x20, x0
    mov x21, x1
    ...

    /* Set exception vectors */
    adr   x0, early_exceptions
    msr   vbar_el1, x0

    /* Invalidate RW data cache */
    adr   x0, __RW_START__
    adr   x1, __RW_END__
    sub   x1, x1, x0
    bl    inv_dcache_range

    /* Zero BSS */
    ...
    bl    zeromem

    /* Set up the stack */
    bl    plat_set_my_stack

    /* Restore args and jump to C */
    mov x0, x20
    mov x1, x21
    mov x2, x22
    mov x3, x23
    bl  bl2_main

    /* Should never return */
    no_ret plat_panic_handler
endfunc bl2_entrypoint
```

## Inside `bl2_main()`

Once the assembly hands off control, we enter `bl2_main()` in `bl2/bl2_main.c`. The function signature itself is interesting — it receives the four register arguments passed from BL1 through the assembly stub, and it carries the `__no_pauth` attribute to ensure it runs before pointer authentication is configured:

```c
void __no_pauth bl2_main(u_register_t arg0, u_register_t arg1,
                         u_register_t arg2, u_register_t arg3)
{
    entry_point_info_t *next_bl_ep_info;

    /* 1. Enable early console if EARLY_CONSOLE flag is enabled */
    plat_setup_early_console();

    /* 2. Perform early platform-specific setup (console, IO, memory layout) */
    bl2_early_platform_setup2(arg0, arg1, arg2, arg3);

    /* 3. Perform generic architectural setup (FP/SIMD access) */
    bl2_arch_setup();

    /* 4. Platform arch setup (MMU, page tables) */
    bl2_plat_arch_setup();

    /* 5. Initialize Pointer Authentication if supported */
    ...

    /* 6. Print version banner */
    NOTICE("BL2: %s\n", build_version_string);
    NOTICE("BL2: %s\n", build_message);

    /* 7. Initialize crypto and authentication modules */
    crypto_mod_init();
    auth_mod_init();

    /* 8. Initialize Measured Boot backend */
    bl2_plat_mboot_init();

    /* 9. Initialize boot source (dynamic config, transfer lists) */
    bl2_plat_preload_setup();

    /* 10. Load the subsequent bootloader images */
    next_bl_ep_info = bl2_load_images();

    /* 11. Teardown Measured Boot and crypto */
    bl2_plat_mboot_finish();
    crypto_mod_finish();

    /* 12. Handoff to next image (path depends on EL3 vs S-EL1) */
    ...
}
```

There are a few things here that are easy to miss but are architecturally important:

- **The NOTICE banners print *before* images are loaded**, not after. By the time you see `"BL2: v2.14..."` on your serial console, the MMU is already up, the console is initialized, and crypto is ready — but no images have been loaded yet.

- **`bl2_arch_setup()` and `bl2_plat_arch_setup()` are different functions**. `bl2_arch_setup()` (in `bl2/aarch64/bl2_arch_setup.c`) is tiny — it just enables FP/SIMD register access by writing to `CPACR_EL1`. The heavy lifting of MMU configuration happens in `bl2_plat_arch_setup()`, which is a platform-provided hook.

- **Crypto and authentication are initialized mid-function**, after the MMU is up but before images are loaded. This is because every image loaded from the FIP will be authenticated (signature-verified) before being used. The `auth_mod_init()` call sets up the chain of trust.

## Early Platform Setup

The very first substantial call in `bl2_main()` is `bl2_early_platform_setup2()`. On ARM reference platforms (in `plat/arm/common/arm_bl2_setup.c`), this function does a remarkable amount of work:

1.  **Console initialization**: Calls `arm_console_boot_init()` to bring up the UART. This is the earliest point at which you'll see serial output from BL2. For platforms using `RESET_TO_BL2=1`, BL2 is the very first stage, so it must initialize the UART itself. In other cases, BootROM or BL1 might have set up a low-speed UART, and BL2 re-initializes it with a higher baud rate or on different pins.

2.  **Memory layout discovery**: When BL1 is present, it passes the Trusted SRAM layout to BL2 via the function arguments (or via a Transfer List in newer firmware handoff schemes). When `RESET_TO_BL2` is set, BL2 uses statically defined values (`ARM_BL_RAM_BASE` and `ARM_BL_RAM_SIZE`) since there's no BL1 to pass this information.

3.  **IO layer initialization**: Calls `plat_arm_io_setup()` to register platform IO devices. This sets up the abstraction layer that BL2 will later use to read from eMMC, SD, NOR flash, or other boot media.

4.  **Generic delay timer initialization**: Calls `generic_delay_timer_init()` at the very end. This is critical — many subsequent hardware initializations depend on having a reliable delay mechanism. 

Notice that the console and delay timer are initialized *before* the MMU is enabled (that happens later in `bl2_plat_arch_setup()`). The UART and timer peripherals can be accessed because the instruction cache is on but the MMU is off — so all addresses are treated as physical addresses with device-nGnRnE memory ordering by default.

## Advanced Memory Mapping

In Day 3, BL1 set up a tiny MMU configuration just for ROM and SRAM. BL2 needs to think bigger.

During `bl2_plat_arch_setup()`, BL2 configures the MMU translation tables for the entire system it cares about. On ARM reference platforms (in `plat/arm/common/arm_bl2_setup.c`), this function defines a static array of memory regions and then calls `setup_page_tables()` followed by `enable_mmu_el1()` (or `enable_mmu_el3()` when `BL2_RUNS_AT_EL3`):

```c
static void arm_bl2_plat_arch_setup(void)
{
    const mmap_region_t bl_regions[] = {
        MAP_BL2_TOTAL,
        ARM_MAP_BL_RO,
        /* ... */
        { 0 }
    };

    setup_page_tables(bl_regions, plat_arm_get_mmap());

#if BL2_RUNS_AT_EL3
    enable_mmu_el3(0);
#else
    enable_mmu_el1(0);
#endif
}
```

If you are porting TF-A to a custom SoC, you will define a static array of memory regions (often named `plat_arm_mmap` or similar). The platform provides this via `plat_arm_get_mmap()`. TF-A uses functions like `mmap_add_region()` (in `lib/xlat_tables_v2/`) to register these mappings. This is crucial because memory-mapped peripherals (like UART or GPIO) must be explicitly mapped with the `MT_DEVICE` memory type attribute to be accessible. If you forget to map a peripheral, any access to it after the MMU is on will trigger a synchronous data abort.

Most importantly, this expanded memory map includes the **DRAM (Dynamic RAM)** controller.

BL2 maps the external DRAM so that it can eventually load massive binaries (like U-Boot and the Linux kernel) into it. Setting up DRAM involves platform-specific code interacting with the SoC's memory controller. 

**Why is DRAM Training so Vendor-Specific?**
Every SoC uses different memory controller IPs (e.g., from Synopsys or Cadence) and connects to RAM on custom PCBs with varying trace lengths. DRAM training involves compensating for these physical characteristics—adjusting DQS delay lines, read/write leveling, and impedance matching. 

TF-A makes this highly customizable by providing the `bl2_platform_setup()` hook. Silicon vendors can simply plug in their proprietary DDR initialization sequence here without modifying the core TF-A boot logic.

## Boot Media & FIP Parsing

With DRAM ready, BL2 prepares to read the next stages of firmware. BL2 uses TF-A's IO Storage framework (`drivers/io/io_storage.c`) to interact with boot media. 

During early platform setup (`bl2_early_platform_setup2()`), the platform initializes drivers for eMMC, SD, NOR flash, or USB via `plat_arm_io_setup()`. For instance, in `plat/st/common/bl2_io_storage.c` (used by STM32MP1), you can see the initialization of MMC and FIP (Firmware Image Package) handlers.

But how does BL2 know *which* media to boot from? Typically, the BootROM or BL1 reads external GPIO pin states (bootstrap pins) to determine the boot source. It then passes this selection to BL2, often via a secure hardware register or shared SRAM. BL2 reads this state during initialization to select the correct IO abstraction layer and fetch the FIP.

## Deferred Platform Setup: A Lazy Initialization Pattern

Here's something that surprised me when I first read the code. You might expect `bl2_platform_setup()` to be called directly from `bl2_main()`. It isn't.

Instead, `bl2_platform_setup()` is called from *inside* `bl2_load_images()` (in `bl2/bl2_image_load_v2.c`), and only when the current image descriptor has the `IMAGE_ATTRIB_PLAT_SETUP` attribute set:

```c
/* Inside bl2_load_images() */
while (bl2_node_info != NULL) {
    if ((bl2_node_info->image_info->h.attr &
        IMAGE_ATTRIB_PLAT_SETUP) != 0U) {
        if (plat_setup_done != 0) {
            WARN("BL2: Platform setup already done!!\n");
        } else {
            INFO("BL2: Doing platform setup\n");
            bl2_platform_setup();
            plat_setup_done = 1;
        }
    }

    /* Load and authenticate the image */
    err = load_auth_image(bl2_node_info->image_id,
                          bl2_node_info->image_info);
    ...
}
```

This is a deliberate design choice. The platform can control *when* during the image loading sequence the full platform setup runs. A `plat_setup_done` flag ensures it only runs once, even if multiple image descriptors carry the attribute. On ARM reference platforms, the `bl2_platform_setup()` function calls `plat_arm_security_setup()`, which initializes the TZC400 — but this can be deferred until right before the first image that needs DRAM is loaded.

## Peripheral Initialization

### 1. Architectural Timers
TF-A relies on delays and timeouts for various hardware initializations—such as waiting for a PLL to lock, waiting for DRAM training to finish, or waiting for an eMMC controller to acknowledge a read. 

The generic delay timer is initialized during early platform setup via `generic_delay_timer_init()` (in `drivers/delay_timer/generic_delay_timer.c`). The actual implementation is more nuanced than you might expect:

```c
void generic_delay_timer_init(void)
{
    assert(is_armv7_gentimer_present());

    /* Value in ticks */
    unsigned int mult = MHZ_TICKS_PER_SEC;

    /* Value in ticks per second (Hz) */
    unsigned int div  = plat_get_syscnt_freq2();

    /* Reduce multiplier and divider by dividing them repeatedly by 10 */
    while (((mult % 10U) == 0U) && ((div % 10U) == 0U)) {
        mult /= 10U;
        div /= 10U;
    }

    generic_delay_timer_init_args(mult, div);
}
```

Rather than directly reading `CNTFRQ_EL0` and passing it to a simple init function, TF-A queries the system counter frequency via the platform hook `plat_get_syscnt_freq2()`, then computes a `mult/div` ratio (reducing it by GCD for precision). This ratio is used internally to convert microsecond delays into counter tick counts. Under the hood, the timer reads `CNTPCT_EL0` (Counter-timer Physical Count register) to measure elapsed time.

Once initialized, BL2 can use functions like `timeout_init_us(1000)` and `timeout_elapsed()` (from `drivers/delay_timer/delay_timer.c`) to safely poll hardware status registers without busy-waiting forever.

### 2. Security Controllers
If the SoC has specialized TrustZone Address Space Controllers (TZASC), BL2 configures them. The reference implementation lives in `plat/arm/common/arm_tzc400.c`, and the setup is triggered via `plat_arm_security_setup()`.

Interestingly, *where* this gets called depends on the system configuration:
- **Without RME**: Called from `bl2_platform_setup()` → `arm_bl2_platform_setup()` (during image loading, as part of the deferred setup pattern described above).
- **With RME (Realm Management Extension)**: Called from `bl2_plat_arch_setup()` → `arm_bl2_plat_arch_setup()` — earlier in the boot, alongside the Granule Protection Table (GPT) initialization.

Why does BL2 do this instead of BL31 or Secure Firmware? Because BL2 is the stage that initializes the DRAM. Once DRAM is online, it is essentially "open" to the whole system. Before BL2 loads any untrusted payloads (like U-Boot) into DRAM, it must immediately partition the memory into Secure and Non-Secure regions. If Non-Secure software tries to access the Secure chunk later, the TZASC will throw a hardware exception.

## The Handoff: Two Very Different Paths

After all images are loaded, authenticated, and measured, BL2 must transfer control to the next boot stage (typically BL31, the Secure Monitor). How it does this depends entirely on whether BL2 is running at EL3 or S-EL1:

**Path 1: BL2 at S-EL1 (the common case)**

BL2 cannot directly jump to BL31 because it doesn't have EL3 privileges. Instead, it issues an **SMC (Secure Monitor Call) back to BL1**, which is still resident in ROM:

```c
/* BL2 at S-EL1: hand back to BL1 via SMC */
smc(BL1_SMC_RUN_IMAGE, (unsigned long)next_bl_ep_info, 0, 0, 0, 0, 0, 0);
```

BL1's SMC handler receives the entry point info for BL31 and performs the EL3-to-EL3 transition. This is an elegant design — BL2 never needs EL3 privileges, yet BL31 gets launched at EL3 through BL1's cooperation.

**Path 2: BL2 at EL3 (`RESET_TO_BL2` or RME)**

When BL2 is already at EL3, it can jump directly to BL31 using `bl2_run_next_image()`, which is an assembly function (in `bl2/aarch64/bl2_run_next_image.S`) that sets up the target EL3 state and performs an exception return:

```c
/* BL2 at EL3: direct jump */
NOTICE("BL2: Booting BL31\n");
bl2_run_next_image(next_bl_ep_info);
```

In both paths, BL2 disables pointer authentication and flushes the console before handing off. The `bl2_main()` function never returns — execution flows to the next image.

---

With the MMU expanded, DRAM trained, boot media mounted, images authenticated, and the UART console humming, BL2 has completed its main mission: parsing the FIP and loading the rest of the firmware. I'll cover the FIP format and image loading in detail tomorrow in Day 5. See you there!
