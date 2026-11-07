# Threading and Processes in XenevaOS

In XenevaOS, multitasking is entirely built on the concept of threads. XenevaOS does not identify processes or user applications directly; instead, it schedules and executes individual threads using CPU resources. The scheduler is the heart of XenevaOS and is responsible for managing threads, handling creation, blocking/unblocking (synchronization), context switching, and terminating threads when required.

---

##  Thread Representation in Kernel

Each thread is represented by the `AA64Thread` structure (under AArch64) or `AuThread` structure (under x86_64), which stores:
- **Thread Context:** Saved CPU registers (e.g., `x19-x30`, `sp`, `elr_el1`, `spsr_el1` for AArch64).
- **Scheduling State:** Ready, Blocked, Sleep, or Killable.
- **Execution Level:** Kernel-mode thread (`THREAD_LEVEL_KERNEL`) or User-mode thread (`THREAD_LEVEL_USER`).
- **User Mode Reference:** A pointer to the `AuUserEntry` structure (if the thread belongs to a user-space process).

---

##  User Mode Entry & Transitions (`AuUserEntry`)

When a thread is scheduled to execute in user mode (EL0), the kernel uses the `AuUserEntry` structure to pass execution context, stack pointers, and command-line arguments to the user program's entry runtime (`crt0`).

### The `AuUserEntry` Structure
```c
typedef struct _uentry_ {
    uint64_t entrypoint;   /* EL0 entry point address */
    uint64_t rsp;          /* User-space stack pointer */
    uint64_t cs;           /* Code segment descriptor / EL0 selector */
    uint64_t ss;           /* Stack segment descriptor / EL0 selector */
    int num_args;          /* Number of arguments (argc) */
    uint64_t argvaddr;     /* User-space VA of the argv[] page (for crt0) */
    uint64_t argvkernel;   /* Kernel-space VA of the same argv[] page (for EL1 writes) */
    char** argvs;          /* Raw string pointers array */
    uint64_t stackBase;    /* Bottom of the user stack */
} AuUserEntry;
```

---

##  Argument Passing Architecture (`argvaddr` vs `argvkernel`)

A critical challenge during process creation is writing the command-line arguments (like `/calc.exe --debug`) from kernel space (EL1) into the newly created user-space process space (EL0) stack or data page.

### The Problem
During process setup (e.g., in `AuLoadExecToProcess`), the CPU is running in EL1, using the kernel's active translation tables (TTBR1). The user-space virtual mapping (specified by `argvaddr`, e.g., `0x0000_xxxx_xxxx`) is defined in the target process's page tables (TTBR0). 
- If the kernel attempts to write directly to `argvaddr`, it will fail with a page fault or a translation fault because the kernel is either not operating under the target process's address space context yet, or the user pages are marked with restrictions.

### The Solution: Double Mapping
To bypass this limitation, XenevaOS uses a double-mapping scheme:
1. **Physical Page Allocation:** The kernel allocates a physical memory page to host the arguments.
2. **User-Space Mapping (`argvaddr`):** This physical page is mapped to a virtual address in the target process's user-space virtual memory (e.g., `0x0000_xxxx_xxxx`). This address is stored in `argvaddr` and is passed to `crt0` so user-space can read the arguments.
3. **Kernel-Space Alias (`argvkernel`):** The same physical page is simultaneously mapped to a virtual address in the kernel's higher-half address space (e.g., `0xFFFF_xxxx_xxxx`). This virtual address is stored in `argvkernel`.
4. **Writing Arguments:** The kernel safely writes the argument strings and the array of pointers using `argvkernel`. Since it is mapped to the same physical backing memory, the data is written directly to the page that the user process will access.
5. **Control Hand-off:** When the thread enters user mode (EL0) and switches to the process's page tables, it reads the arguments using `argvaddr` natively.

This decoupled memory mapping design eliminates dependency on EL0 page table accessibility during process initialization, ensuring robust and crash-safe process bringup.