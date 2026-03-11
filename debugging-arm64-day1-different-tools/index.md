---
date: "2025-07-14"
title: 'Debugging an Embedded System - Day 1 - Introducing different Tools'
thumbnail: "/posts/debugging-arm64-day1-different-tools/thumbnail.jpg"
author: "Mbharmal"

product: "embedded-engineering"

tags:
  - "ARM64"
  - "OpenOCD"
  - "Debugging"
  - "GDB"
  - "JTAG"

categories:
  - "arm64"
  - "debugging"

---

Let's now actually understand the need and usage of GDB, OpenOCD, JTAG, SWD, etc. Let's go over some of the use-cases to see how all of them come together and form a coherent debugging environment for a typical embedded system.

<!--more-->

Alright, now that we know what the [general flow and pattern of debugging looks like](https://pyjamacafe.com/posts/debugging-arm64-day0-problem-setup/), let's now see what all different tools we have in our arsenal.

## GDB: The High-level Interrogator

### What is GDB?

Continuing and building on our analogy from the [previous article](https://pyjamacafe.com/posts/debugging-arm64-day0-problem-setup/), GDB is like the lead detective who asks pointed questions to uncover what’s happening inside the suspect (your code). This is primarily done to gather more clues and evidences.

GDB (GNU Debugger), is a powerful, open-source debugger that lets us interact with a running program (if you're debugging a process on a self-hosted environment).
While debugging an embedded system, GDB allows developers to analyze and troubleshoot firmware or any bare-metal code directly on their target hardware by controlling program execution, inspecting memory and register values, and analyzing runtime behaviors. Throughout this series we will cover how that is achieved.

GDB comes in two flavors when debugging embedded systems: **GDB client** and **GDB server**.

* **GDB Client**: This is the interface we use on our development machine. It’s where we type commands like `break`, `step`, or `print` to control the debugging process. Think of it as the detective’s notepad, where we jot down questions and interpret answers.
* **GDB Server**: This runs closer to the target hardware (sometimes on the target itself or on a debugging adapter). It’s the translator that takes our high-level questions jotted in notepad and turns them into low-level instructions the hardware understands. It’s like the detective’s field agent, gathering raw evidence from the crime scene.

#### Why Two Parts?

The split between GDB client and server exists because embedded systems often run on resource-constrained devices that can’t handle a full-fledged debugger. The GDB server acts as a lightweight intermediary, communicating with the hardware over a protocol like JTAG or SWD (more on those later). This separation allows us to debug remotely, which is critical when working with embedded devices.

#### Why It Matters?

GDB lets us pause execution, inspect variables, step through code line by line, and even modify the program’s state on the fly.

Unlike printf-style debugging (which is like scattering sticky notes around the crime scene), GDB gives us a structured way to probe the system without altering its behavior too much. For e.g., setting a breakpoint is like pausing time to examine the scene in detail, something print statements can’t  easily manage to do.

**NOTE**: One often-overlooked aspect of GDB is its flexibility. It’s not just for ARM or embedded systems, it works across platforms, architectures and and languages (C, C++, Rust, etc.).

But in embedded debugging, its usually paired with tools like OpenOCD, which bridges the gap between GDB’s high-level commands and the low-level hardware.

***

## OpenOCD: The Guy close to the Hardware

### What is OpenOCD?

OpenOCD (Open On-Chip Debugger) is an open-source tool that acts as a middleman between our debugging software (like GDB) and the target hardware. It’s closer to the hardware, translating high-level debugging commands into signals the chip understands. OpenOCD typically runs on our development machine or a debug adapter and communicates with the target via protocols like JTAG or SWD.

#### What Does It Do?

OpenOCD is responsible for tasks like:

* Relaying and transcribing GDB commands which the specific hardware can understand and getting the responses back.
* Initializing the target hardware (e.g., resetting the CPU) or atleast the test logic of CPU core.
* Flashing firmware onto the chip.

Think of OpenOCD as the logistics coordinator in our detective analogy. While GDB is asking questions, OpenOCD is the one setting up the crime scene, ensuring the right tools are in place, and shuttling evidence back and forth.

#### Why It’s Critical?

Without OpenOCD, we’d have no way to talk to the hardware. Modern SoCs are complex, with multiple cores, memory regions, and peripherals.

OpenOCD understands the specific debug architecture of the chip (like ARM’s CoreSight, which we’ll cover in a later article) and provides a standardized interface for GDB to interact with it. This abstraction saves us from writing custom low-level code for every chip we debug.

Think of OpenOCD as a polyglot translator who usually accompanies cross country negotiations. The GDB client speaks one language (high-level debugging commands), but the hardware only understands its native tongue (low-level signals). OpenOCD ensures everyone’s on the same page.

***

## Actual hardware Connectors

If we were debugging on the same machine we are running about _to be debugged program_ then we probably would not require this. For e.g. let's say we are debugging some user-space C/C++ code, then we can natively debug in the same machine we are executing.

But, often times, embedded systems are very much resource constrained. Typically running on a bare-metal or an RTOS envrionment we don't have liberty to run a **GDB server** or **OpenOCD** like utility. Heck even if our embedded setup is capable of running Linux, it is often stripped to barebones and devoid of any debug capabilities which it might otherwise have supported.

In these scenarios, we will have to run these utilities on the host side and connect to the embedded device which is being debugged through something else, well mostly wires. There are protocols or interfaces defined which are designed for this specific need. Two of the major and most famous ones are:

* JTAG
* SWD

Because the industry came together and standarized the debug infrastructure and communication interfaces, it really eases the life of designers. If a system supports JTAG or SWD then it is guaranteed that it can be interfaced by any debugger which is capable of talking over these protocols.

In this one we will gloss over the key details of both of these, while we will uncover them in detail in the upcoming articles.

### JTAG

#### What is JTAG?

JTAG (Joint Test Action Group) is a hardware interface standard used for debugging and testing integrated circuits. It’s a physical and logical protocol that lets us access the internals of a chip.

#### How Does It Work?

JTAG uses a set of pins (typically 4 or 5: TDI, TDO, TMS, TCK, and sometimes TRST) to form a serial communication chain. This chain allows us to:

* Read and write CPU registers.
* Control the processor’s execution (start, stop, step).
* Access memory and peripherals.
* Reset the CPU core or testing infrastructure

In the ARM world, JTAG is often used with CoreSight, a debug architecture that enhances JTAG’s capabilities for complex SoCs.

#### Why It’s Useful?

JTAG is like a master key that unlocks the chip’s inner workings. It’s especially powerful in multi-core systems, where we need to debug multiple processors simultaneously. It’s also widely supported, making it a go-to choice for many debug adapters and tools.

***

### SWD

#### What is SWD?

SWD (Serial Wire Debug) is a two-wire (lol) debugging protocol developed by ARM as a leaner alternative to JTAG. It uses just two pins: SWDIO (data) and SWCLK (clock). It’s designed specifically for ARM’s Cortex cores and is optimized for simplicity and efficiency.

#### How Does It Differ from JTAG?

While JTAG is a multi-pin, multi-purpose protocol, SWD is streamlined:

* Fewer pins (2 vs. 4–5 for JTAG), saving valuable space on the chip.
* Simpler protocol, making it faster for basic debugging tasks.
* Still supports core debugging features like breakpoints, register access, and memory reads/writes.

However, SWD is ARM-specific and doesn’t support the same level of chip-level testing (like boundary scan) that JTAG does.

It’s perfect for modern microcontrollers where pin count is a premium. Most ARM Cortex-M devices support SWD, and it’s often the default choice for hobbyist and professional debuggers alike.

One advantage of SWD is its robustness in noisy environments. With fewer pins, there’s less chance of signal interference, which is a common issue in JTAG setups with long cables or complex boards.

***

## Summary

Let’s revisit our detective analogy to see how these tools work together:

* **GDB** is the lead detective, issuing commands and analyzing clues.
* **OpenOCD** is the logistics coordinator, translating information between the detective and the crime scene.
* **JTAG** and **SWD** are the communication channels or interfaces, like radio or phone lines, that connect the coordinator to the crime scene (the chip).

When we debug an ARM-based system, we typically:

1. Connect a debug adapter (like an ST-Link, J-Link or FTDI based) to the target via JTAG or SWD.
2. Run OpenOCD to initialize the hardware and establish a connection.
3. Launch the GDB client, which talks to OpenOCD to control the target.

NOTE: We haven't talked about ST-Link, J-Link or FTDI yet. To be concise, they are some specialized connectors which provide one end to be connected to our host machine (usually USB port based) and other end has a connector which can be plugged to our embedded board. Mind, these connectors still work over and implement JTAG and/or SWD.

We will go over these connectors in one of our pratical sessions.

Here’s a quick example of starting a GDB session:

```bash
# At this point, you should have connected the JTAG or SWD interfaces between your host machine and the embedded system
# Typically,we use some debuggers like ST-Link, J-Link or some FTDI chip to facilitate the
# translation from your USB port to the JTAG or SWD interface which ultimately connects to your embedded board.

# Start OpenOCD with the configuration files
openocd -f interface/<your_interface>.cfg -f board/<your_board>.cfg

# Launch GDB
arm-none-eabi-gdb my_to_be_debugged_program.elf

# Connect to the GDB server spawed by OpenOCD
(gdb) target extended-remote localhost:3333

# Some example commands with which we can interact with the CPU or SoC
(gdb) monitor reset init # We will go over these commands in detail

(gdb) break main # Sets breakpoint at the first instruction of main

(gdb) continue # Start the execution and it will stop when the breakpoint is hit
```

This sequence sets up the debugging environment, connects to the target, resets it, sets a breakpoint at `main`, and starts execution. It’s a simple flow, but each step relies on the interplay of GDB, OpenOCD, and JTAG/SWD.

***

In our next article, we’ll dive deeper into why a debug view is so much more powerful than print statements in embedded systems. We will also connect to our own systems implement JTAG/SWD through FTDI chips and connect to the debug logic of a ARM A72 CPU core.

We’ll also start exploring ARM’s CoreSight architecture, which takes these tools to the next level.

Stay tuned till then!
