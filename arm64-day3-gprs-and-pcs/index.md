---
date: "2025-07-06"
title: 'ARMv8a Procedure Call standard and GPRs'
thumbnail: "/posts/arm64-day3-gprs-and-pcs/arm64-gprs-and-pcs.png"
author: "Mbharmal"

product: "embedded-engineering"

tags:
  - "ARM64"
  - "Exception Models"
  - "ABI"
  - "PCS"

categories:
  - "arm64"

---

ABI and Procedure call standard are backbone to safe-guard compatibility and inter-operability. Let's see how and why.

<!--more-->
# ARMv8-A Procedure Call standard

## What Are General-Purpose Registers?

Previous articles, we have solely dedicated ourselves in the study of [exception levels](https://pyjamacafe.com/posts/arm64-exception-levels/) and [interrupt handling](https://pyjamacafe.com/posts/arm64-irq-handling/).

Let's now dive into nuances of what the core has to offer and the so-called contract or interface between high level software and mostly external but low level code/libraries. We as developers often don't have to deal with this as it is taken care by compilers and various other toolchain components.

But, today we are gonna deep dive into one of the most crucial pieces of any ABI specification i.e PCS (Procedure Call standard)

The breif outline will be:
* A brief about GPRs (General Purpose registers)
* ARM64 specific Procedure call standar
  * Few tricky bits - Understanding via an example
* GPR handling during various scenarios
  * Exception and Interrupt Handling
  * Context Switch Handling

## A Brief about GPRs

In the ARMv8-A architecture, general-purpose registers (GPRs) are the primary and often the fatest data stores of the CPUs. They’re like the sticky notes where they're used in storing data, passing arguments and moving data around. We will go over the examples in detail.

Since we are focusing on ARM64 in the AArch64 execution state (the 64-bit mode of ARMv8-A), we get **31 general-purpose registers**, labeled **X0 to X30**. Each of these is **64 bits wide**.

Note that each of these registers also has a **32-bit alias**, named **W0 to W30**. When we access a register as, say, W0, we’re only looking at the lower 32 bits of X0. The upper 32 bits? They’re just chilling, ignored by the operation, and sometimes zeroed out depending on the instruction.

This feature is a core one in preserving the backward compatibility and optimizing for 32-bit operations when we don’t need the full 64-bit range.

Oh, the **zero register** (XZR in 64-bit, WZR in 32-bit). It’s not one of the 31 GPRs but acts like a register that always reads as zero and discards any writes. This comes in handy in surpringly frequent scenarios as we will soon see.

Why did the GPRs have to go to therapy? They seemed to have Dual personality disorder after being used as both X0 and W0 in the same program :)

## 1. ARM AArch64 ABI and Function Calls

The ABI is like the rulebook for how software talks to the hardware. In AArch64, the GPRs are assigned specific roles during function calls to make things efficient and consistent.

This becomes very much necessary if the sources are coming already compiled (possibly from a totally different toolchain environment) and only linked against our source (statically or at run-time). More on this in an example below.

  ```c
  // Let's say we are using a 3P library (already compiled, source not available) in our application
  // This library helps us render some graphics. Assume a data-stucture of how it stores point infor
  // looks something like this.

  // This is just a header file:"pyjamabrah_graphics.h"

  typedef struct {
      double x, y, z; // 3 * 8 bytes = 24 bytes
      long timestamp; // 8 bytes
  } 3d_point;

  // It also exports a function to create point for us (sort of like a constructor)
  3d_point create_point(double x, double y, double z, long time) {
      3d_point point;
      point.x = x;
      point.y = y;
      point.z = z;
      point.timestamp = time;
      return point;
  }
  ```

```c
  // Now, this is our application code where we actually reference the library

#include "pyjamabrah_graphics.h"

  int main() {
      3d_point our_point = create_point(1.0, 2.0, 3.0, 123456789);
      // Our own logic
  }
```

This is a very common use-case and you'll find it pretty much everywhere.

Few key points to note are:
* The data-structure being returned  by callee (`create_point()`) to the caller (`main()`) is larger than 16-bytes. Hence we can't fit in X0 and X1.
* The code for `create_point()` is assumed to be coming from an external library.
  * This also means we might not have the source for that. It might be statically or dynamically get linked to our application.
  * This also means we can't assume anything about when, where or how is the source compiled. It may have used a completely different toolchain. e.g. `clang-llvm` while our source (`main()`) is compiled with `gcc`.

With these points, there is definitely going to creeping comptability issues if not handled carefully. This is where a tight, restrictive and totally unambigous ABI specification comes in-handy.

Let's look at how these problems are tackled. But, first let take a glimpse at ARM64 ABI focusing on the guidelines during function calls.

* **X0–X7**: These are the **parameter and result registers**. When we call a function, we pass the first eight arguments in these registers. If the function returns a value, it usually comes back in X0 (or X0–X1 for larger types). This is super efficient because we’re not constantly hitting the stack for arguments.

* **X8**: This is the **indirect result register**. It’s used when a function returns a really big structure (like a struct larger than 16 bytes in C). Instead of cramming it into X0–X1, the caller allocates memory and passes a pointer to that memory in X8, where the callee stores the result.

* **X9–X15**: These are **temporary registers**. They’re free for the function to use and trash without worrying about saving them. Think of them as scratchpads for quick calculations \[ARM Cortex-A Series Programmer’s Guide, page 9-2 (PDF page 116)].

* **X16–X17**: These are the **intra-procedure call temporary registers** (IP0 and IP1). They’re used for things like veneers (small code stubs for branching) or other temporary needs during a function call. This is an interesting topic on its own. We will explore this in detail some other time.

* **X18**: This one’s a bit of a wildcard, often reserved as a **platform register**. The operating system might use it for specific purposes, like thread-local storage in some systems.

* **X19–X28**: These are **callee-saved registers**. If a function wants to use them, it has to save their values before messing with them and restore them before returning. This makes them great for holding data that needs to stick around across function calls.

* **X29**: This is the **frame pointer (FP)**. It points to the base of the current stack frame, helping us navigate the stack during function calls. It’s a lifesaver for debugging and unwinding the stack.

* **X30**: The **link register (LR)**. When we call a function with a `bl` (branch with link) instruction, the return address gets stored here. When the function’s done, it uses `ret` to jump back to the address in X30.

* **SP**: The **stack pointer**. It’s not one of the 31 GPRs but a special register that keeps track of the stack’s top. We use it for pushing and popping data, like local variables or saved registers.

Let's particulary focus on the intent of **X8** register and how it can be used as indirect result register. Infact, this will come in handy in our example while returning the **3d_point** structure.

In assembly, the caller allocates stack space for the 32-byte `3d_point`, passes its address in X8, and the callee writes the structure to that address. Let's take a look at the final assembly.

  ```armasm
  // Function: create_point
  // Arguments: x0 (double x), x1 (double y), x2 (double z), x3 (long time)
  // X8: Pointer to memory for return value (3d_point, 32 bytes)
  // Returns: x0 (pointer to result, same as x8)

  create_point:
      str d0, [x8]         // Store x (double in d0) at x8+0
      str d1, [x8, #8]     // Store y (double in d1) at x8+8
      str d2, [x8, #16]    // Store z (double in d2) at x8+16
      str x3, [x8, #24]    // Store timestamp (long in x3) at x8+24
      mov x0, x8           // Return the pointer in x0
      ret                  // Return using x30 (LR)

  // Caller code (e.g., in main)
  main:
      sub sp, sp, #32      // Allocate 32 bytes for 3d_point
      mov x8, sp           // x8 points to result storage
      fmov d0, #1.0        // x = 1.0
      fmov d1, #2.0        // y = 2.0
      fmov d2, #3.0        // z = 3.0
      mov x3, #123456789   // timestamp
      bl create_point      // Call function
      add sp, sp, #32      // Deallocate stack
      // Do something productive
      mov x0, #0
      ret
  ```

**callee-saved vs. caller-saved** distinction. If we’re calling a function, we need to assume X0–X17 might get trashed, so we'll save them if we need their values later. On the other hand, if our function uses X19–X28, we’re responsible to save and restore them.

## 2. Exception and Interrupt Handling

We have sort of covered this in our earlier post when we talked at length on [Interrput handlining](https://pyjamacafe.com/posts/arm64-irq-handling/). But, let's revisit them briefly here for the sake of completing the overall picture.

When an exception or interrupt hits, the processor needs to save the current state so it can pick up where it left off after handling the event. This is where GPRs play a critical role, but there’s a catch: not all registers are automatically saved.

In AArch64, exceptions (like interrupts, aborts, or system calls) cause a switch to a higher **Exception Level** (EL1, EL2, or EL3). The processor saves some state automatically in system registers like the **Exception Link Register (ELR)** and **Saved Process Status Register (SPSR)**, but the GPRs? That’s on us (or the exception handler) to handle.

Here’s how it works:

* **ELR\_ELn**: Stores the return address (like X30 but for exceptions).
* **SPSR\_ELn**: Saves the processor state (like condition flags and interrupt masks).
* **GPRs**: The exception handler typically saves X0–X30 (or a subset) to the stack before doing anything else. Why? Because the handler might need to use those registers, and we don’t want to clobber the original program’s data.

Code snippet for save and restore:

```armasm
// Exception handler entry

exception_handler:
    // Save all GPRs to the stack
    stp x0, x1, [sp, #-16]!  // Push x0, x1
    stp x2, x3, [sp, #-16]!  // Push x2, x3
    // ... Save x4–x29 similarly
    stp x30, xzr, [sp, #-16]! // Push x30 (LR), xzr as padding

    // Handle the exception (e.g., interrupt processing)
    bl handle_interrupt

    // Restore GPRs
    ldp x30, xzr, [sp], #16   // Pop x30, discard padding
    // ... Restore x4–x29
    ldp x2, x3, [sp], #16     // Pop x2, x3
    ldp x0, x1, [sp], #16     // Pop x0, x1

    eret                      // Return from exception
```

A common oversight is not saving **X30 (LR)** in the exception handler. Since X30 holds the return address for function calls, an interrupt handler might overwrite it if it calls another function.

### 3. Context Switching and GPRs

While we are trying to be pedantic, let's also quickly go over the context switch code and what all needs to stored and restored there.

In a context switch, the OS typically saves:

* **All 31 GPRs (X0–X30)**: Because we don’t know which registers the task is using.
* **SP**: The stack pointer, so the task can pick up its stack where it left off.
* **PC (via ELR)**: The program counter, so the task resumes at the right instruction.
* **PSTATE (via SPSR)**: The processor state, including condition flags.

Here’s a simplified context switch routine:

```armasm
// Save current task's context

save_context:
    stp x0, x1, [sp, #-16]!   // Save x0, x1
    // ... Save x2–x29
    stp x30, xzr, [sp, #-16]! // Save x30
    mrs x0, sp_el0            // Save stack pointer
    str x0, [x1]              // Store SP to task control block
    mrs x0, elr_el1           // Save return address
    str x0, [x1, #8]          // Store ELR to task control block
    mrs x0, spsr_el1          // Save processor state
    str x0, [x1, #16]         // Store SPSR to task control block
    ret

// Restore next task's context
restore_context:
    ldr x0, [x1, #16]         // Load SPSR
    msr spsr_el1, x0          // Restore processor state
    ldr x0, [x1, #8]          // Load ELR
    msr elr_el1, x0           // Restore return address
    ldr x0, [x1]              // Load SP
    msr sp_el0, x0            // Restore stack pointer
    ldp x30, xzr, [sp], #16   // Restore x30
    // ... Restore x2–x29
    ldp x0, x1, [sp], #16     // Restore x0, x1
    eret                      // Return to task
```

I will see you folks in the next one!
