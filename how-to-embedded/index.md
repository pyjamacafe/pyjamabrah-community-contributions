---
# Change the Date below
date: "2025-10-01"

# Add the title of the Post here
title: 'How to Embedded Systems?'

# Add short description of the article for SEO optimization
description: "what does embedded systems mean and how to go about learning those."

# replace the "sample/cover.png" with path to a 16:9 image
# this image will be shown in SEO optimization and when
# you share the post on social media.
thumbnail: "/posts/how-to-embedded/cover.png"

# Add your GitHub handle here
author: "streetdogg"

# Add tags as suitable for the topic
tags:
  - "what is embedded systems"

# If it fits a certain category of topics, add that
categories:
  - "basics"

# If this is a Series of posts, Add the name of the series
series:
  - "how to embedded"
---

Embedded systems are specialized electronic setups designed to handle specific tasks by sensing environmental conditions, processing the data, and affecting the surroundings through actuators. These systems are integrated into larger devices to control particular functions.

<!--more-->

Sensors detect physical changes like temperature, pressure, or light and convert them into electrical signals, such as voltage or current shifts, which the system can understand. Actuators take electrical signals from the system and turn them into physical actions, like movement from motors, light from LEDs, or displays on screens.

The processing unit acts as the core, analyzing sensor data, making decisions, and sending commands to actuators to achieve the system’s goal.

Essentially, embedded systems connect the physical and electrical worlds by sensing changes, processing them, and producing actions.

For example, in a smart thermostat, a sensor measures room temperature, the processor compares it to a target, and an actuator controls the heater. These systems power countless devices, from household appliances to complex automotive or medical systems, and are built to be efficient and reliable, often with limited resources.

---

## Processing

Embedded systems process data through two primary approaches:

1. Analog
1. Digital

Analog processing relies on components like transistors, capacitors, and resistors to handle continuous signals, such as voltage that varies over time, allowing direct manipulation of these signals to produce an output.

Digital processing, by contrast, samples continuous signals at discrete intervals, converting them into data points that digital components like CPUs or logic gates can process. This sampling transforms real-world inputs, like voltage changes from a pressure sensor, into manageable digital data.

Modern embedded systems predominantly use digital processing because CPUs offer greater speed and efficiency compared to analog methods.

In digital systems, CPUs like ARM Cortex-M or RISC-V execute instructions in a sequence: fetching data, processing it, and triggering actions. These systems employ sequential circuits with state management to handle computations, ensuring precise control over the embedded system’s tasks.

As an example, a digital system might sample a temperature sensor’s voltage, process the data to determine if heating is needed, and send a command to an actuator. This reliance on digital processing enables embedded systems to perform complex, efficient, and reliable operations in devices ranging from simple sensors to advanced automotive controls.

---

## Key Components

Embedded systems rely on several core components to function effectively, each playing a distinct role in sensing, processing, and acting on environmental data.

### 1. Sensors

Sensors transform physical quantities, such as temperature or force, into electrical signals like voltage or current that the system can interpret. For instance, a temperature sensor might produce a voltage corresponding to the ambient heat.

### 2. Actuators

Actuators, on the other hand, take electrical signals from the system and convert them into physical actions, such as controlling motor speed or updating a display. There’s some debate about whether screens qualify as actuators since they primarily serve as output devices, presenting information visually rather than causing mechanical change, but they’re often categorized this way in embedded systems.

### 3. CPU

The CPU forms the heart of modern embedded systems, managing the flow of data from sensing to processing and actuation. Popular CPUs like ARM Cortex-M and RISC-V, both 32-bit architectures, are widely used due to their efficiency and value for learning system design.

Together, these components enable embedded systems to sense environmental changes, process data, and produce precise physical responses, powering devices from simple gadgets to complex machinery.

---

## Learning Embedded Systems

Mastering these concepts, from the programmer’s model to communication protocols, equips developers to build efficient and reliable embedded systems for a wide range of applications.

### Programmer’s Models

At the core is the programmer’s model, which serves as a critical abstraction for understanding how to program a CPU, much like knowing how a wall socket delivers electricity. This model encompasses the instruction set, which defines the commands the CPU can execute, configuration registers that control features like interrupts, and data registers that store information during processing. To grasp this model, regularly consult CPU manuals from manufacturers like ARM or Intel, focusing on the table of contents and key sections to build familiarity over time.

The exception model explains how the CPU manages interrupts, such as pausing a program to address an urgent event like a sensor trigger.

The debug model covers tools and techniques for troubleshooting CPU operations, ensuring code runs correctly.

The memory model describes how the CPU interacts with various memory types, from the fastest registers to slower caches, DRAM, SSDs, and hard disks. Techniques like caching help bridge the speed gap between the CPU and memory, boosting performance by storing frequently accessed data closer to the processor.

### Languages

Programming embedded systems primarily relies on the C language, which dominates firmware development due to its efficiency and maintainability.

Assembly language is useful for low-level, CPU-specific tasks like cache management but becomes impractical for complex programs due to maintenance challenges.

Emerging languages like C++ and Rust are gaining traction but remain less common in this field.

A solid understanding of the toolchain, including compilers, assemblers, linkers, and debuggers, is also essential, as these tools transform code into executable programs and help identify errors during development.

### Operating Systems

Operating systems play a significant role in enabling a single CPU to juggle multiple tasks, such as sensing temperature, updating a display, and triggering a beep.

The scheduler, a core component of an operating system, manages these tasks by switching between threads or processes. Operating system primitives like mutual exclusion, semaphores, and message queues ensure shared resources are handled without conflicts.

FreeRTOS, a popular real-time operating system, exemplifies this by being adaptable to new CPUs, making it a valuable tool for embedded developers.

### Communication Protocols

Communication protocols are another critical area, as they enable sensors and actuators to connect with the CPU. Starting with simpler protocols like UART, I2C, and SPI provides a foundation before tackling more complex ones like USB or PCIe.

These protocols define schemes for data transmission, including start and stop signals or error correction methods, ensuring reliable communication between components.

---

## Projects

Exploring embedded systems through practical projects provides hands-on experience that deepens understanding of their core concepts. These projects bridge theoretical knowledge with practical application, preparing you for real-world challenges in embedded systems design.

### Write a Scheduler

One effective project involves writing a scheduler for a CPU, such as an ARM Cortex-M, to manage multiple tasks. This requires programming in assembly and C while applying knowledge of the CPU’s programmer’s model and exception model to handle task switching and interrupts. By building a scheduler, you gain insight into operating system fundamentals and learn how CPUs execute and prioritize tasks, offering a practical grasp of system-level operations.

### Implement a Digital Filter

Another valuable project focuses on audio processing by interfacing a microphone and speaker with a CPU to implement a signal processing algorithm, such as filtering drum noise to isolate guitar sounds. This task demands managing large volumes of data in real time and configuring communication buses like I2C or SPI to connect the components. Through this project, you develop skills in handling real-time constraints, processing continuous data streams, and integrating hardware with software, all of which are critical for embedded systems development.
