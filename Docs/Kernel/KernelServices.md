# Kernel Service Calls

The XenevaOS Service Call Interface (SCI) provides a controlled gateway for user-space applications to request services from the kernel. Service calls act as the bridge between user mode and kernel mode, and can be thought of as Kernel IPC. Service calls allow applications to perform privileged operations such as file access, memory management, process control, and network communication. In XenevaOS, all user applications interact with the kernel through a well-defined set of system calls that follow a consistent calling convention and interface layout.

## Calling Convention

System calls in XenevaOS are invoked using the ``syscall`` instruction (on x86_64) or ``svc #0`` (on ARM64). <br><br>
The system call number is placed in the `R12` register, and arguments are passed through registers `R13, R14, R15, RDI`, and the stack. Upon completion, the return value is stored in the `RAX` register.

For ARM64, the system call number is placed in the `X16` register, and arguments are passed through registers `X0, X1, X2, X3, X4, and X5`. Upon completion, the return value is stored in the `X6` register. <br><br>

### Example (x86_64):
```
ExitProcess:
      mov r12, 5
	  mov r13, 0
	  mov r14, 0
	  mov r15, 0
	  mov rdi, 0
	  syscall
	  ret
```

### Example (ARM64):
```
ProcessSleep:
      mov x16, 23
	  svc #0
	  mov x0, x6
	  ret
```

## Kernel Service Calls

The kernel maintains a service call dispatch table, which maps each kernel handler function to its respective system call number.

| Service Number | Symbol Name | Description | 
|----------------|-------------|-------------|
| 0 | ``null_call`` | _Does nothing, returns 0_ |
| 1 | ``SeTextOut/UARTDebugOut`` | _Uses Kernel serial console output to print user space messages_ |
| 2 | ``PauseThread`` | _Puts the currently running thread into the blocked queue_ |
| 3 | ``GetThreadID`` | _Returns currently running thread ID_ |
| 4 | ``GetProcessID`` | _Returns current running process ID_ |
| 5 | ``ProcessExit`` | _Terminate and deallocate all resources of current running process_ |
| 6 | ``ProcessWaitForTermination`` | _Puts the currently running process into the wait queue until the desired process terminates (which is specified by process ID in the arguments)_ |
| 7 | ``CreateProcess`` | _Creates a new process slot in the kernel_ |
| 8 | ``ProcessLoadExec`` | _Loads an executable into the provided process slot_ |
| 9 | ``CreateSharedMem`` | _Creates a shared memory segment in the current process slot_ |
| 10 | ``ObtainSharedMem`` | _Maps and returns a pointer to the shared memory address mapped to the physical addresses of the provided shared memory segment_ |
| 11 | ``UnmapSharedMem`` | _Unmaps a shared memory segment from the current process (the shared memory segment is not freed until it is detached from all processes)_ |
| 12 | ``OpenFile`` | _Opens a file in the kernel and returns its file descriptor allocated in the current process slot_ |
| 13 | ``CreateMemMapping`` | _Creates a memory mapping inside a process of memory, file or device_
| 14 | ``UnmapMemMapping`` | _Unmaps a previously mapped memory within a process. It requires the memory pointer_ |
| 15 | ``GetProcessHeapMem`` | _Process on Aurora maintains heap memory separately, i.e it uses separate address range from MemMappings_ |
| 16 | ``ReadFile`` | _Reads a file to a given buffer, it requires the file descriptor number previously allocated using ``OpenFile`` call_ |
| 17 | ``WriteFile`` | _Writes data to a file from given buffer, it requires the file descriptor number previously allocated using ``OpenFile`` call_ |
|20 | ``CloseFile`` | _Closes a given file, requires the file descriptor allocated using ``OpenFile`` call_ |
|21 | ``FileIoControl``| _Submit commands to a given file, mostly used in device files_ |
|22 | ``FileStat`` | _Get metadata of a file to a given buffer, metadata holds information such as file size, creation/modificatin date, current position and EndOfFile bit_ |
|23 | ``ProcessSleep`` | _Put a process on sleep for given amount of time, it accepts number of scheuler ticks_ |
|24 | ``AuGetSystemTimerTick`` | _Get the current number of timer ticks passed since the system was booted_ |






