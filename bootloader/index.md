---
date: "2025-05-27"
title: 'Bootloaders on Cortex-M CPUs'
thumbnail: "/posts/bootloader/cover.jpeg"
author: "streetdogg"

product: "embedded-engineering"

tags:
  - "bootloader"
  - "cortex-m"

categories:
  - "microcontroller"
---

Bootloaders are the unsung heroes of ARM Cortex-M-based systems, ensuring reliable startup, application execution, and firmware updates. By leveraging the Cortex-M’s vector table, VTOR, and low-power features, developers can create efficient and secure bootloaders tailored to their application’s needs.

<!--more-->

ARM Cortex-M processors, designed for low-power, cost-sensitive embedded systems, are widely used in microcontrollers (MCUs) for applications like IoT devices, wearables, and industrial sensors. A critical component in these systems is the bootloader, a specialized piece of firmware responsible for initializing the hardware and loading the main application.

## What is a Bootloader?

A bootloader is a small program that runs when an MCU powers on or resets. Its primary tasks are:
- Hardware Initialization: Configuring essential peripherals, clocks, and memory.
- Application Loading: Transferring control to the main application firmware.
- Firmware Updates: Supporting over-the-air (OTA) or wired updates for new application code.

On ARM Cortex-M CPUs, the bootloader operates in a resource-constrained environment, often residing in non-volatile memory (e.g., Flash) and executing with limited RAM.

## ARM Cortex-M Boot Process

The boot process on ARM Cortex-M processors follows a well-defined sequence, leveraging the CPU's architecture and memory organization. Here’s a step-by-step breakdown:

### Reset and Vector Table Lookup

When the MCU powers on or resets, the Cortex-M CPU initializes its Program Counter (PC) and Stack Pointer (SP) using values from the vector table, a data structure stored in Flash memory (typically at address `0x00000000`). The vector table contains:
- Initial Stack Pointer (SP): The first word (at `0x00000000`) sets the stack’s top address.
- Reset Vector: The second word (at `0x00000004`) points to the address of the reset handler, the entry point of the bootloader or application.

The CPU fetches the reset vector and jumps to the reset handler, which is usually part of the bootloader.

### Bootloader Execution

The reset handler begins executing the bootloader code, which performs the following:
- System Clock Setup: Configures the MCU’s clock sources (e.g., internal RC oscillator, external crystal) and PLL to set the CPU frequency.
- Memory Initialization: Sets up SRAM, clears the BSS segment (uninitialized data), and copies initialized data from Flash to RAM.
- Peripheral Initialization: Enables essential peripherals like GPIO, UART, or SPI, depending on the bootloader’s requirements (e.g., for communication during firmware updates).
- Boot Mode Decision: Checks for conditions to enter bootloader mode (e.g., a button press, a flag in memory, or a communication signal) or proceed to the application.

### Application Handover

If the bootloader determines that the main application should run, it:
- Validates the Application: Checks the application’s integrity (e.g., using a checksum or CRC) to ensure it’s not corrupted.
- Sets Up the Application’s Vector Table: Cortex-M CPUs support a Vector Table Offset Register (`VTOR`), which allows the bootloader to point to the application’s vector table (often located at a different Flash address, e.g., `0x00008000`).
- Jumps to the Application: Loads the application’s reset vector address into the PC and transfers control. This typically involves setting the SP to the application’s stack pointer and jumping to its reset handler.

### Firmware Update Mode (Optional)

If the bootloader enters update mode, it:
- Communicates with a Host: Uses protocols like UART, USB, or CAN to receive new firmware.
- Erases and Writes Flash: Programs the new firmware into the designated Flash region.
- Verifies the Update: Ensures the new firmware is correctly written before rebooting.

## Key Features of Cortex-M Bootloaders

ARM Cortex-M CPUs have specific features that influence bootloader design:

### Memory Layout
- Flash Memory: Cortex-M MCUs typically have a single Flash region divided into bootloader and application areas. The bootloader resides at the start of Flash (0x00000000) to align with the default vector table location.
- SRAM: Limited RAM (often 4–256 KB) requires efficient bootloader code to minimize memory usage.
- Memory Protection Unit (MPU): Some Cortex-M CPUs (e.g., Cortex-M3, M4, M7) include an MPU, which the bootloader can configure to restrict access to sensitive memory regions.

### Vector Table Relocation

The `VTOR` register is critical for bootloaders. By default, the vector table is at 0x00000000, but the bootloader can set `VTOR` to point to the application’s vector table, enabling seamless transitions without modifying the CPU’s default boot behavior.

### Low-Power Considerations

Cortex-M CPUs are optimized for low power. Bootloaders must minimize execution time and power consumption, especially in battery-powered devices. This involves:
- Using low-power clock sources initially.
- Disabling unused peripherals.
- Entering sleep modes when waiting for update commands.

### Security Features

Modern Cortex-M MCUs (e.g., Cortex-M33) include security features like TrustZone or Flash protection. Bootloaders can:
- Lock Flash regions to prevent unauthorized access.
- Implement secure boot by verifying firmware signatures using cryptographic algorithms.

## Challenges in Bootloader Development

Developing bootloaders for Cortex-M CPUs involves several challenges:
- Size Constraints: Bootloaders must be compact to fit in limited Flash (often 8–32 KB).
- Reliability: The bootloader must handle power failures, corrupted firmware, or communication errors gracefully.
- Compatibility: The bootloader must support multiple application versions and MCU variants.
- Debugging: Bootloaders run before debug interfaces are fully initialized, making debugging difficult.

## Simple Bootloader Workflow

Here’s a simplified pseudo-code representation of a Cortex-M bootloader:

```c
void reset_handler(void) {
    // Initialize system clock
    configure_clock();

    // Initialize SRAM (clear BSS, copy .data)
    initialize_memory();

    // Check boot mode (e.g., button press or flag)
    if (check_bootloader_mode()) {
        // Enter firmware update mode
        initialize_uart();
        receive_firmware();
        erase_flash();
        write_firmware();
        verify_firmware();
    } else {
        // Validate application
        if (validate_application()) {
            // Set VTOR to application’s vector table
            SCB->VTOR = APP_VECTOR_TABLE_ADDRESS;
            // Load application’s stack pointer
            set_sp(*(uint32_t *)
                   APP_VECTOR_TABLE_ADDRESS);
            // Jump to application’s reset handler
            jump_to(*(uint32_t *)
                   (APP_VECTOR_TABLE_ADDRESS + 4));
        }
    }
    // Fallback: halt or reset
    while (1);
}
```

## Practical Considerations

- Toolchains: Use tools like ARM Keil, IAR Embedded Workbench, or GCC-based toolchains (e.g., arm-none-eabi-gcc) for bootloader development.
- Linker Scripts: Customize linker scripts to place the bootloader and application in separate Flash regions and define the vector table correctly.
- Testing: Test bootloaders thoroughly, including edge cases like power loss during updates, to ensure robustness.

Bootloaders are the unsung heroes of ARM Cortex-M-based systems, ensuring reliable startup, application execution, and firmware updates. By leveraging the Cortex-M’s vector table, VTOR, and low-power features, developers can create efficient and secure bootloaders tailored to their application’s needs. Understanding the boot process and architectural nuances is essential for building robust embedded systems that power the modern world.
