---
date: "2026-06-19"
title: 'ARM TF-A Day 5: BL2 Deep Dive - Image Loading Framework'
thumbnail: "/posts/arm-tfa-day5-bl2-loading/thumbnail.png"
author: "Mbharmal"
product: "embedded-engineering"
tags:
  - "ARM64"
  - "TF-A"
  - "BL2"
  - "FIP"
categories:
  - "arm-tfa"
---

Hey there, welcome back! In Day 4, BL2 successfully initialized the DRAM and the UART console. The platform is stable. Now, BL2 must execute its primary mission: loading the remaining firmware stages into memory and passing the baton to the Secure Monitor.

<!--more-->

## The Firmware Image Package (FIP)

BL2's most complex logic lies inside `bl2_load_images()` (defined in `bl2/bl2_image_load_v2.c`). 

To load the images, BL2 uses TF-A's `load_auth_image()` framework. It reads the FIP (Firmware Image Package) from flash memory.

### What is a FIP?
The FIP is an open-source standard format designed by the TF-A project (originally ARM) to package multiple binaries and certificates into a single continuous blob. Instead of managing half a dozen separate flash partitions for each individual payload, everything is bundled together. This dramatically simplifies the boot media layout, the flashing process, and the bootloader's I/O drivers, as BL2 only needs to read from one partition.

At build time, TF-A uses a host-side C utility called `fiptool` (located in `tools/fiptool`) to concatenate all these separate files into the final `fip.bin`. A complete build command looks something like this: 
`fiptool create --tb-fw bl2.bin --soc-fw bl31.bin --tos-fw bl32.bin --nt-fw u-boot.bin fip.bin`.

> [!TIP]
> **Wait, is BL2 also in the FIP?**
> Yes! You might notice `--tb-fw bl2.bin` in the command above. In the standard TF-A boot architecture, the BootROM loads and executes BL1. BL1 then initializes the flash drivers, parses this exact same FIP to extract the BL2 binary, and executes it. BL2 then takes over, parses the *same* FIP, and extracts the remaining payloads (BL31, BL32, BL33).

> [!IMPORTANT]
> **Binary Format (No ELFs!)**
> You might wonder if the FIP contains standard `.elf` files. The answer is **no**. TF-A does *not* parse ELF files in BL2 because BL2 is designed to be as minimal as possible. During the build process, the Makefiles run `objcopy -O binary` to strip all ELF headers and sections, leaving only the raw, executable machine code (`.bin`). BL2's loader then blindly copies these raw bytes from the FIP offset directly into the target memory address.

### The FIP On-Disk Format

The FIP's binary layout is defined in `include/tools_share/firmware_image_package.h`. It starts with a small fixed-size header, followed by an array of ToC (Table of Contents) entries:

```c
/* include/tools_share/firmware_image_package.h */

/* Magic signature to identify a valid FIP */
#define TOC_HEADER_NAME  0xAA640001

typedef struct fip_toc_header {
    uint32_t    name;           /* Must be TOC_HEADER_NAME (0xAA640001) */
    uint32_t    serial_number;  /* Non-zero for a valid FIP */
    uint64_t    flags;          /* Platform ToC flags in bits [32:47] */
} fip_toc_header_t;

typedef struct fip_toc_entry {
    uuid_t      uuid;           /* 16-byte UUID identifying the payload */
    uint64_t    offset_address; /* Byte offset of the payload within the FIP */
    uint64_t    size;           /* Size of the payload in bytes */
    uint64_t    flags;          /* Entry-specific flags */
} fip_toc_entry_t;
```

So the FIP is essentially a flat binary: a `fip_toc_header_t` followed by an array of `fip_toc_entry_t` records (terminated by a null UUID entry), followed by the raw payload data blobs one after another. Each ToC entry maps a standard 16-byte UUID to a byte offset and size within the package.

### The IO Storage Abstraction
If you look closely at `drivers/io/io_fip.c`, you will notice it follows a classic **filesystem-like design**. It registers an `io_dev_funcs_t` struct containing function pointers like `.open`, `.read`, `.size`, and `.close`:

```c
/* drivers/io/io_fip.c */
static const io_dev_funcs_t fip_dev_funcs = {
    .type      = device_type_fip,
    .open      = fip_file_open,
    .seek      = NULL,
    .size      = fip_file_len,
    .read      = fip_file_read,
    .write     = NULL,
    .close     = fip_file_close,
    .dev_init  = fip_dev_init,
    .dev_close = fip_dev_close,
};
```

This is part of TF-A's highly abstracted generic IO storage framework. This framework allows TF-A's higher-level `load_auth_image()` function to be completely agnostic to the storage medium—it treats a FIP exactly the same way it treats an eMMC block device or a Memmap!

### The Complete Loading Flow

Here is the complete bootloader execution flow from platform setup to bytes in RAM:

1. **Registration:** During platform setup, the platform registers the FIP driver by calling `register_io_dev_fip()`. For example, QEMU does this in `plat/qemu/common/qemu_io_storage.c`.
2. **Routing:** The platform uses an IO policy framework to map Image IDs (e.g., `BL31_IMAGE_ID`) to this FIP IO device.
3. **Loading:** `bl2_load_images()` (in `bl2/bl2_image_load_v2.c`) iterates through the platform's list of required images. For each one, it calls `load_auth_image()`.
4. **FIP Init & Validation:** Inside the IO framework, `fip_dev_init()` reads the FIP header from the backend storage and validates the magic number:

```c
/* drivers/io/io_fip.c - fip_dev_init() */
static inline int is_valid_header(fip_toc_header_t *header)
{
    if ((header->name == TOC_HEADER_NAME) && (header->serial_number != 0)) {
        return 1;
    } else {
        return 0;
    }
}
```

5. **UUID Lookup:** When `load_auth_image()` calls `.open` for a specific image, `fip_file_open()` seeks past the header and iterates through the ToC entries, comparing each entry's UUID against the requested one:

```c
/* drivers/io/io_fip.c - fip_file_open(), simplified */
/* Seek past the FIP header into the Table of Contents */
io_seek(backend_handle, IO_SEEK_SET, sizeof(fip_toc_header_t));

do {
    io_read(backend_handle, &current_fip_file.entry,
            sizeof(current_fip_file.entry), &bytes_read);

    if (compare_uuids(&current_fip_file.entry.uuid,
                      &uuid_spec->uuid) == 0) {
        found_file = 1;   /* UUID match! */
    }
} while ((found_file == 0) &&
         (compare_uuids(&current_fip_file.entry.uuid, &uuid_null) != 0));
```

6. **Read:** Once the entry is found, subsequent `.read` calls (`fip_file_read()`) seek to the payload's `offset_address` within the FIP and copy the raw bytes into the target memory buffer.

### What Does BL2 Load?

BL2 systematically searches the FIP for the following critical binaries:
1.  **BL31 (EL3 Runtime Software):** The Secure Monitor. This is the TF-A component that provides PSCI and SMC routing. *(Note: On some platforms like NXP i.MX or Allwinner, U-Boot SPL acts as the loader (replacing BL1/BL2), but SPL itself is not the EL3 runtime monitor. It loads TF-A's BL31.)*
2.  **BL32 (Secure-EL1 Payload):** The Trusted OS. Common examples include OP-TEE (maintained by TrustedFirmware.org) or Trusty (Google's Little Kernel-based TEE). We will dive deep into OP-TEE in upcoming articles.
3.  **BL33 (Non-Trusted Firmware):** The Normal World Bootloader (e.g., U-Boot, UEFI).

## Loading and Memory Layout

As BL2 iterates through the FIP, it loads each binary into a highly specific memory region. 

> [!NOTE] 
> **How does BL2 know where to load them?** The FIP itself **does not** contain load addresses, only offsets within the blob. The memory map is dictated by the platform. BL2 uses `bl_mem_params_node_t` descriptors that are either defined statically by the platform port (e.g., in `plat/<vendor>/<board>/include/platform_def.h`) or discovered dynamically via the Firmware Configuration (fconf) framework.

This is where the TrustZone architecture shines:

*   **BL31** is loaded into **Secure SRAM** (or highly restricted Secure DRAM). Because it will run at EL3, it must be protected from everything else in the system.
*   **BL32** is loaded into **Secure DRAM**. It needs more memory than SRAM can provide because it's a full OS, but it still must be isolated from the Normal World.
*   **BL33** is loaded into **Non-Secure DRAM**. This is the wild west. BL33 has no access to Secure memory.

## Authentication and Hardware Encryption Hooks (TBBR)

BL2 doesn't call a simple `load_image()` and hope for the best. The actual function it calls for each image is `load_auth_image()` (defined in `common/bl_common.c`). This function wraps the raw loading with a recursive authentication framework.

If Trusted Board Boot Requirements (TBBR) are enabled, `load_auth_image()` calls `load_auth_image_recursive()` which walks up the certificate chain — loading and verifying parent certificates before the image itself:

```c
/* common/bl_common.c - load_auth_image_recursive() */
static int load_auth_image_recursive(unsigned int image_id,
                                     image_info_t *image_data)
{
    /* Use recursion to authenticate parent images first */
    rc = auth_mod_get_parent_id(image_id, &parent_id);
    if (rc == 0) {
        rc = load_auth_image_recursive(parent_id, image_data);
        if (rc != 0) return rc;
    }

    /* Load the image */
    rc = load_image(image_id, image_data);
    if (rc != 0) return rc;

    /* Authenticate it */
    rc = auth_mod_verify_img(image_id,
                             (void *)image_data->image_base,
                             image_data->image_size);
    if (rc != 0) {
        /* Authentication failure! Zero the memory immediately. */
        zero_normalmem((void *)image_data->image_base,
                       image_data->image_size);
        flush_dcache_range(image_data->image_base,
                           image_data->image_size);
        return -EAUTH;
    }

    return 0;
}
```

Notice the security discipline: if authentication fails, the loaded image bytes are **immediately zeroed and flushed** from cache before returning the error. This prevents a partially-loaded, unauthenticated image from lingering in memory where it could be exploited.

TF-A provides a highly modular cryptography framework (`drivers/auth/crypto_mod.c`) allowing SoC vendors to either use a software fallback or wire up their physical hardware accelerators.

### Software Authentication (Raspberry Pi Example)
By default, TF-A uses an embedded software crypto library (mbedTLS). For example, the Raspberry Pi 3/4 ports simply include `drivers/auth/mbedtls/mbedtls_crypto.mk`. 

When included, the mbedTLS wrapper registers its software hooks into TF-A's crypto framework via the `REGISTER_CRYPTO_LIB` macro. The actual registration call has multiple variants depending on the configured `CRYPTO_SUPPORT` level. Here is the most common one, with full auth + hash + AES-GCM decryption:

```c
/* drivers/auth/mbedtls/mbedtls_crypto.c */
#define LIB_NAME  "mbed TLS"

#if CRYPTO_SUPPORT == CRYPTO_AUTH_VERIFY_AND_HASH_CALC
#if TF_MBEDTLS_USE_AES_GCM
REGISTER_CRYPTO_LIB(LIB_NAME, init, verify_signature, verify_hash, calc_hash,
                    auth_decrypt, NULL, NULL);
#else
REGISTER_CRYPTO_LIB(LIB_NAME, init, verify_signature, verify_hash, calc_hash,
                    NULL, NULL, NULL);
#endif
```

The 8 arguments to `REGISTER_CRYPTO_LIB` are: `name`, `init`, `verify_signature`, `verify_hash`, `calc_hash`, `auth_decrypt`, `convert_pk`, and `finish`. BL2 then uses these software hooks to verify the RSA/ECDSA signature of the certificate, and checks the SHA-256 hash of the binary.

### Hardware Crypto (STM32MP1 Example)
However, relying purely on software crypto leaves the symmetric decryption key vulnerable in memory. High-security platforms instead use physical hardware to fetch keys (like from OTP fuses) and dedicated hardware accelerators for cryptographic operations.

The STM32MP1 platform is a fantastic example. Instead of registering the generic mbedTLS library, STMicroelectronics registers its *own* crypto library that uses hardware peripherals directly:

```c
/* plat/st/common/stm32mp_crypto_lib.c */
static void crypto_lib_init(void)
{
    /* Initialize the STM32 hardware hash accelerator */
    stm32_hash_register();

    /* Initialize hardware: RNG, Secure AES (SAES), and PKA engines */
    stm32_rng_init();
    stm32_saes_driver_init();
    stm32_pka_init();
}

REGISTER_CRYPTO_LIB("stm32_crypto_lib",
                    crypto_lib_init,
                    crypto_verify_signature,   /* Uses stm32_pka_ecdsa_verif() */
                    crypto_verify_hash,        /* Uses stm32_hash_final_update() */
                    NULL,
                    crypto_auth_decrypt,       /* Uses stm32_saes for AES-GCM */
                    crypto_convert_pk,
                    NULL);
```

For firmware encryption, TF-A provides the `plat_get_enc_key_info()` hook so platforms can securely fetch decryption keys from hardware. STM32MP1 implements this to read the key from OTP (One-Time Programmable) fuses word-by-word:

```c
/* plat/st/common/stm32mp_crypto_lib.c - plat_get_enc_key_info() */
int plat_get_enc_key_info(enum fw_enc_status_t fw_enc_status, uint8_t *key,
                          size_t *key_len, unsigned int *flags,
                          const uint8_t *img_id, size_t img_id_len)
{
    /* Look up the OTP fuse index for the encryption key */
    stm32_get_otp_index(ENCKEY_OTP, &otp_idx, &otp_len);

    /* Read the key from OTP fuses, one 32-bit word at a time */
    for (i = 0U; i < read_len / sizeof(uint32_t); i++) {
        stm32_get_otp_value_from_idx(otp_idx + i, &otp_val);
        tmp = bswap32(otp_val);  /* Byte-swap for endianness */
        memcpy(key + i * sizeof(uint32_t), &tmp, sizeof(tmp));
    }

    /* Derive the final 32-byte key (duplicates if OTP is only 16 bytes) */
    derive_key(key, key_len, read_len, flags, img_id, img_id_len);
    return 0;
}
```

In contrast, the Raspberry Pi ports have **no hardware crypto** — they rely entirely on the software mbedTLS library. You can verify this: `plat/rpi/rpi3/platform.mk` includes only `drivers/auth/mbedtls/mbedtls_crypto.mk` and never overrides `plat_get_enc_key_info()`.

If any image fails authentication or decryption, the boot process halts immediately to protect the system.

## The Handoff to BL31

With BL31, BL32, and BL33 securely in memory, BL2 needs to hand over control to BL31.

### Flushing and Preparing the Parameters

Before jumping, BL2 must ensure that all loaded image data and the parameter structures are visible in physical RAM (not trapped in cache lines that BL31's fresh MMU context won't see). This happens in two stages:

1. **Per-image flush:** Inside `load_auth_image()` (`common/bl_common.c`), after each image is successfully loaded and authenticated, TF-A immediately flushes that image's data from cache to main memory:

```c
/* common/bl_common.c - load_auth_image() */
/* Flush the image to main memory so that it can be executed
 * later by any CPU, regardless of cache and MMU state. */
flush_dcache_range(image_data->image_base, image_data->image_size);
```

2. **Parameter descriptor flush:** After all images are loaded, `bl2_load_images()` calls `plat_flush_next_bl_params()`, which flushes the `bl_params_t` data structures. This structure is how BL2 tells BL31 where BL32 and BL33 were loaded:

```c
/* common/desc_image_load.c - flush_bl_params_desc() */
void flush_bl_params_desc(void)
{
    flush_dcache_range((uintptr_t)mem_params_desc_ptr,
        sizeof(*mem_params_desc_ptr) * mem_params_desc_num);

    flush_dcache_range((uintptr_t)next_bl_params_ptr,
        sizeof(*next_bl_params_ptr));
}
```

The `bl_params_t` structure is a linked list of `bl_params_node_t` entries, each containing the `image_id`, `image_info`, and `ep_info` (entry point info) for one executable image. BL31 will receive the address of this structure in register `x0` and parse it using `bl31_params_parse_helper()` to extract the BL32 and BL33 entry points.

### The Two Handoff Paths

Finally, BL2 triggers the handoff. The mechanism depends entirely on which Exception Level BL2 is running in. You can see both paths clearly in `bl2/bl2_main.c`:

### Path 1: BL2 runs at Secure-EL1 (Classic Flow)
In the traditional flow, BL1 is still resident in EL3, waiting. BL2 (running at S-EL1) has no way to directly jump to BL31 because it lacks the privilege to set `elr_el3`. Instead, BL2 executes a Secure Monitor Call (`SMC`) to trap back up into EL3. BL1 intercepts this SMC, reads the provided entry point info, and jumps to BL31.

Here is the exact C code from `bl2_main.c` that triggers this trap:

```c
/* bl2/bl2_main.c (when !BL2_RUNS_AT_EL3) */

/* Disable PAuth before handing over */
if (is_feat_pauth_supported()) {
    pauth_disable_el1();
}

console_flush();

/*
 * Run next BL image via an SMC to BL1. Information on how to pass
 * control to the BL32 (if present) and BL33 software images will
 * be passed to next BL image as an argument.
 */
smc(BL1_SMC_RUN_IMAGE, (unsigned long)next_bl_ep_info, 0, 0, 0, 0, 0, 0);
```

### Path 2: BL2 runs directly at EL3 (Modern Flow)
In modern flows (like `RESET_TO_BL2` where BL1 is completely bypassed), BL2 runs directly in EL3. Since it's already at the highest privilege level, it won't execute an SMC. Instead, it calls `bl2_run_next_image()` — a small assembly routine that tears down the EL3 environment and jumps to BL31.

Here is the C code that initiates this path:

```c
/* bl2/bl2_main.c (when BL2_RUNS_AT_EL3) */

NOTICE("BL2: Booting BL31\n");
print_entry_point_info(next_bl_ep_info);
console_flush();

/* Disable pointer authentication before running next boot image */
if (is_feat_pauth_supported()) {
    pauth_disable_el3();
}

bl2_run_next_image(next_bl_ep_info);
```

And here is the complete assembly routine that performs the actual jump — this is `bl2/aarch64/bl2_run_next_image.S` in its entirety:

```armasm
/* bl2/aarch64/bl2_run_next_image.S */
func bl2_run_next_image
    mov x20, x0

    /* MMU needs to be disabled because both BL2 and BL31 execute
     * in EL3, and therefore share the same address space.
     * BL31 will initialize the address space according to its
     * own requirement. */
    bl  disable_mmu_icache_el3
    tlbi alle3

    /* Give the platform a last chance to do cleanup */
    bl  bl2_plat_prepare_exit

    /* Load the BL31 entry point (PC) and SPSR from the ep_info struct */
    ldp x0, x1, [x20, #ENTRY_POINT_INFO_PC_OFFSET]
    msr elr_el3, x0
    msr spsr_el3, x1

    /* Populate argument registers x0-x7 from the ep_info args array */
    ldp x6, x7, [x20, #(ENTRY_POINT_INFO_ARGS_OFFSET + 0x30)]
    ldp x4, x5, [x20, #(ENTRY_POINT_INFO_ARGS_OFFSET + 0x20)]
    ldp x2, x3, [x20, #(ENTRY_POINT_INFO_ARGS_OFFSET + 0x10)]
    ldp x0, x1, [x20, #(ENTRY_POINT_INFO_ARGS_OFFSET + 0x0)]

    /* Jump to BL31! */
    exception_return
endfunc bl2_run_next_image
```

A few things to appreciate about this assembly:
- The arguments are loaded in **reverse order** (x6/x7 first, x0/x1 last) because loading x0 last means x0 contains the `bl_params_t` pointer that BL31 expects as its first argument.
- `exception_return` (which expands to the `eret` instruction) atomically restores `SPSR_EL3` into `PSTATE` and branches to the address in `ELR_EL3` — dropping execution into BL31.
- `bl2_plat_prepare_exit` gives the platform one last hook to do any final cleanup (e.g., disabling DMA engines, securing memory regions) before control is irrevocably transferred.

And with that `eret`, BL2's job is complete. It has provisioned the entire memory landscape and handed over the keys to the castle. Tomorrow, in Day 6, we pivot slightly: I will show you how the Raspberry Pi 5 completely bypasses BL1 and BL2! Catch you in the next one!
