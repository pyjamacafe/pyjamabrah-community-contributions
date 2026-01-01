---
date: "2025-07-13"
title: 'Debugging an Embedded System - Day 0 - Understanding What is Debugging'
thumbnail: "/posts/debugging-arm64-day0-problem-setup/thumbnail.jpg"
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

GDB, OpenOCD, JTAG, SWD etc are terms often thrown around when it comes to debugging in an embedded environment. They are naught but just tools made available to us. We will actually understand the core principles and ideas behind debugging such that the usage of such tools would become evident and obvious.

<!--more-->

# On Debugging an Embedded System

This series is going to focus on whats, whys and hows of debugging embedded systems world, particularly those based on ARM-A based architectures.

We might have come across terms like GDB, OpenOCD, JTAG, SWD. If I am able to do justice to the topic, we will hopefully be able to in this series:
* Explore, understand and reason through what is it they bring to the table and why are they required.
* Understand different topologies in which they are or can be connected
* Introduce the ARM debug infrastructure - CoreSight, ETM, CTI
* Code snippets to show some of the points and configs
* \[Stretch Goal\] - Program and connect **Raspberry Pi** as our custom debug probe

***

### Why is Debugging, Anyway?

Before we jump the gun, let's convince ourselves why debugging is even required.

**"I always write such a pristine code that it never has those pesky bugs"**. If you can claim this and back your claim, then may be debugging is something you shouldn't be too much bothered about. But, then a master such as yourself, would be frequently asked to help debug some peasant's (like myself) code. So, in all, it's a good skill to have anyway.

## What does debugging a typical situation look like?

Debugging is like being a detective in a crime drama. Your code is the crime scene, it is behaving weirdly. You know like it’s crashing, or just not doing what you expect. I would define the process of debugging roughly as,

"Debugging is the process of finding the culprit (the bug) by figuring out the clues (the stack traces for e.g.), analyzing evidence (the registers and memory contents) and then forming a theory to explain the observations and findings. In this one we will focus on one such comparatively simpler example where this flow is seen.

In embedded systems, this gets a bit trickier because we’re often dealing with low-level hardware, limited resources, and complex systems interacting with multiple components. But, we also have some very awesome tools at our disposal which we will get introduced to in this article.

### The Crime Scene: A Crash

We have a simple code with two threads (we have code for all in different files):
* **Thread 1** Does something
* **Thread 2** Does something
* **Main** Spawns these threads and waits for them to complete.

What we know: The crash happens while **Thread 2** is executing. Let's look at the code snippets.

**Thread 1**'s code file:
```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

extern char *shared_ptr;

void *thread1_func(void *arg) {
    // Simulate some delay (e.g., initialization work)
    for (volatile int i = 0; i < 100000; i++); // Busy wait
    shared_ptr = malloc(sizeof(char) * 4); // Allocate memory of 4 chars
    shared_ptr[2] = 'P'; // Initialize the char at 2nd offset
    printf("Thread 1: Initialized the character with value %c\n", shared_ptr[2]);
    return NULL;
}
```

**Thread 2**'s code file:
```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

extern char *shared_ptr;

void *thread2_func(void *arg) {
    char value = shared_ptr[2]; // Read the char at 2nd offset
    printf("Thread 2: Read character %c\n", value);
    return NULL;
}
```

**Main**'s code file:
```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

char *shared_ptr = NULL;

int main() {
    pthread_t thread1, thread2;

    pthread_create(&thread1, NULL, thread1_func, NULL);
    pthread_create(&thread2, NULL, thread2_func, NULL);

    // Wait for threads to complete
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    if (shared_ptr) free(shared_ptr);
    return 0;
}
```

### The clues and evidences: Registers and Stack trace

Let's take a look at the evidences and clues at our disposal. This would be a typical crash report when an ARM-A class core crashes.

```asm
*** Data Abort Exception ***

Exception occurred at: 0x0000000000008b20 ; Where the fault happened
Fault Address (FAR_EL1): 0x0000000000000002 ; This is a crucial clue, as we are accessing the 2nd offset of 'shared_ptr'

Exception Syndrome Register (ESR_EL1): 0x96000006
  - EC: 0x25 (Data Abort, synchronous)
  - ISS: 0x00000006 (Translation fault, level 2)
  - IL: 1 (32-bit instruction)
  - DFSC: 0x06 (Translation fault, second level)

Register State:
  x0 : 0x0000000000000000
  x1 : 0x0000000000000002
  x2 : 0x0000000000000000
  x3 : 0x0000000000000000
  x4 : 0x000000007efffd10
  x5 : 0x0000000000000000
  x6 : 0x0000000000000000
  x7 : 0x0000000000000000
  x8 : 0x0000000000000000
  x9 : 0x0000000000000000
  x10: 0x0000000000000000
  x11: 0x0000000000000000
  x12: 0x0000000000000000
  x13: 0x0000000000000000
  x14: 0x0000000000000000
  x15: 0x0000000000000000
  x16: 0x0000000000000000
  x17: 0x0000000000000000
  x18: 0x0000000000000000
  x19: 0x0000000000000000
  x20: 0x0000000000000000
  x21: 0x0000000000000000
  x22: 0x0000000000000000
  x23: 0x0000000000000000
  x24: 0x0000000000000000
  x25: 0x0000000000000000
  x26: 0x0000000000000000
  x27: 0x0000000000000000
  x28: 0x0000000000000000
  x29: 0x000000007efffd00  ; Frame pointer
  x30: 0x0000000000008a50  ; Link register (return address)
  sp : 0x000000007efffd00  ; Stack pointer
  pc : 0x0000000000008b20  ; Program counter

  spsr_el1: 0x0000000060000005
  elr_el1: 0x0000000000008b20

    - Mode: EL0 (User mode), AArch64, Interrupts enabled
```

Let's also take a look at stack trace and dis-assembly of a few instructions at **pc**.

```asm
Stack Trace:
  #0  0x0000000000008b20 in thread2_func(...)
  #1  0x0000000000008a50 in pthread_create_wrapper (...)

Few lines of assembly at the crash site:
0x00008b18 <thread2_func+8>:  ldr    r0, [pc, #12]     ; Load address of shared_ptr into r0
0x00008b1c <thread2_func+12>: ldr    r0, [r0]          ; Load shared_ptr value (base address) into r0
0x00008b20 <thread2_func+16>: ldr    r2, [r0, #2]      ; Dereference shared_ptr[2] (load from address in r0)
```

**Key Clues**:
* `r0` is 0x00000000, confirming that `shared_ptr` is NULL when Thread 2 tries to dereference it.
* `FAR_EL1`(Fault Address Register) shows the content as 0x2, that means the offset 2 was added while dereferencing which is confirmed the code snippet `char value = shared_ptr[2]; // Read the char at 2nd offset`. This further confirms our hypothesis that `shared_ptr` is NULL.
* The program counter (pc) points to the `ldr r2, [r0, #2]` instruction.
* Stack trace confirms supports this hypothesis as well.

**Our Theory**:
Remember, we have figured out that the crash is due to NULL-pointer exception. Now our job is explain why does it happen. For that we usually try to hypothesis few theories which explain the actual cause taking into account the clues and findings.

Let's take a look at the code snippet and ask the question:

* When can `shared_ptr` be NULL?
  * One possible Answer: When it is not initialized by **Thread 1** and **Thread 2** has used it.
    * When can that happen: When **Thread 2** executes before **Thread 1** and de-references `shared_ptr`.

Now, that's sounds like a potential case which can happen and which would result in a similar crash as ours. That's a very good start.

Once we have arrived at a potential theory or hypothesis which seems plausible, we should try to see if it works.

In our case, let's try to fix this. One of the quickest ways to verify would be to just add a check before dereferencing.

```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

extern char *shared_ptr;

void *thread2_func(void *arg) {
    if (shared_ptr) {
        char value = shared_ptr[2]; // Read the char at 2nd offset only if shared_ptr is non-NULL
        printf("Thread 2: Read character %c\n", value);
    }
    return NULL;
}
```

With this change, if our theory is valid and true, we have prevented the crash.

Once we have verified on the field that it actually works, we can then think of a more robust and a better solution that this.

Why don't you try to think of a better way to resolve this? Ideally, we would want **Thread 2** to always read the value and print it no matter when it executes. Think on how can we achieve this?
**Hint**: Think of adding some synchronization primitives.

If you have a solution (may be this or something unique entirely), I would love to hear from you. You can reach out to us at [support@pyjamacafe.com](mailto:support@pyjamacafe.com)

Now that we have looked at a general flow of debugging, in the next ones, we will now introduce different tools we have in our arsenal which will aid a lot in our debugging and detective journey.

See you folks in the next one!
