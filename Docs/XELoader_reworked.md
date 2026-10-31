# XELoader: Complete Documentation

## The Dynamic Linker and Loader for Xeneva OS

---

## TABLE OF CONTENTS

1. [System Overview](#system-overview)
2. [Core Data Structures](#core-data-structures)
3. [Function Call Hierarchy](#function-call-hierarchy)
4. [Detailed Function Reference](#detailed-function-reference)
5. [API Interactions](#api-interactions)
6. [Complete Example: Calculator App](#complete-example-calculator-app)
7. [Memory Management Flow](#memory-management-flow)

---

## SYSTEM OVERVIEW

### Purpose

XELoader is Xeneva OS's dynamic linker and loader. It transforms an executable file on disk into a fully-loaded, linked, and executable process ready for CPU execution.

### What is Dynamic Linking and Loading

Dynamic Linking and Loading is to perform the link and load operations in the runtime rather than before it, and is primarily used for applications that use shared libraries.

- Loading and Linking the shared libraries at runtime for performance optimization and reducing memory usage
- Maps libraries to appropriate randomly assigned addresses to prevent hardcoded memory vulnerabilities

### Core Responsibilities

- Load PE (Portable Executable) format binaries from disk into memory
- Relocate code and data sections to actual runtime addresses (ASLR support - Address Space Layout Randomization)
- Discover and recursively load dynamic library dependencies
- Resolve function symbols between binaries (linking)
- Transfer control to the main executable's entry point

### Key Characteristics

- **Architecture**: Supports ARM64 and x86-64
- **Base Addressess**: 0x600000 for x86 and 0x600000000 for ARM64 addresses are used for statically linked libraries
- **Format**: PE32+ (64-bit) with PE32 compatibility
- **PE-Header**: Supports PE headers of (4096 bytes)
- **ASLR Support**: Each binary loads at random address
- **Recursive Resolution**: Handles indirect dependencies automatically

---

## CORE DATA STRUCTURES

### 1. XELoaderObject Structure

A fundamental structure that tracks the state of each loaded binary:

```cpp
typedef struct _XELDR_OBJ_ {
    char *objname;              // Pointer to object name/path string
    bool loaded;                // State flag: FALSE initially, TRUE after _KeMemMap() calls
    bool linked;                // State flag: FALSE initially, TRUE after linking complete
    uint32_t len;               // Total memory size allocated for this object
    size_t load_addr;           // Base address where object loaded in virtual memory    
    size_t entry_addr;          // Entry point address (for executables only)   
    struct _XELDR_OBJ_ *next;   // Pointer to next object in doubly-linked list
    struct _XELDR_OBJ_ *prev;   // Pointer to previous object in doubly-linked list
} XELoaderObject;
```

### 1'. XELoaderObject Structure

```cpp
typedef struct _XELDR_OBJ_ {
    char *objname;              // File path/name (e.g., "/myapp.exe")
    bool loaded;                // Whether loaded into memory
    bool linked;                // Whether dependencies resolved
    uint32_t len;               // Total memory size allocated
    size_t load_addr;           // Base address in virtual memory
    size_t entry_addr;          // Entry point (for executables)
    struct _XELDR_OBJ_ *next;   // Next in doubly-linked list
    struct _XELDR_OBJ_ *prev;   // Previous in list
} XELoaderObject;
```

---

## FUNCTION CALL HIERARCHY

Main execution flow showing how functions are called:

```
main(argc, argv)
    ├─ XELdrInitObjectList()
    ├─ XELdrCreateObj(argv[0])
    ├─ XELdrStartProc(argv[0], mainobj)
    │   ├─ _KeOpenFile()
    │   ├─ _KeMemMap() - Map sections
    │   ├─ XELdrRelocatePE()
    │   └─ XELdrCreatePEObjects()
    ├─ XELdrLoadAllObject()
    ├─ XELdrLinkDepObject()
    ├─ XELdrLinkAllObject()
    └─ Jump to entry point
```

---

## DETAILED FUNCTION REFERENCE

### Object Management Procedure

| Function                                            | Description                                                                                                                                   |
| --------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| XELdrInitObjectList()                               | Initializes empty object tracking list. Sets global *obj_first* and *obj_last* to NULL.                                                       |
| XELdrCreateObj(char* objname)                       | Creates new loader object with given string name and creates the object structure(46 bytes) with a pointer set to it.                         |
| XELdrCheckObject(const char* name)                  | Searches object list for object by filename from full path, returning TRUE if found, else FALSE.                                              |
| XELdrGetObject(const char* name)                    | Retrieves object pointer from list by name. Returns NULL if not found.                                                                        |
| XELdrLoadObject(XELoaderObject *obj)                | Loads object from disk into memory. Calls XELdrStartProc internally. sets the *loaded*, *load_addr* and *len* variables. Returns 0 on success |
| XELdrStartProc(char* filename, XELoaderObject *obj) | Core loading function. Loads PE binary from disk to memory.                                                                                   |

**Operations**:

1. Open file with _KeOpenFile()
2. Map PE header (4096 bytes)
3. Parse PE headers in-memory
4. Calculate relocation addresses for ASLR
5. Load each section via _KeMemMap()
6. Apply relocations with XELdrRelocatePE()
7. Discover dependencies with XELdrCreatePEObjects()
8. Update object state

**Page Mapping Strategy**:

1. Standard page size: 4096 bytes
2. Each section loaded page-by-page
3. Code sections: read-only
4. Data sections: copy-on-write

**ASLR Relocation Flow**:

1. Kernel picks random base address via _KeMemMap()
2. XELoader calculates delta = actual - ImageBase
3. XELdrRelocatePE() applies delta to all relocatable addresses
4. Result: Binary works at any address

---

### Kernel Calls

**File Operations**:

- `_KeOpenFile()` - Opens file
- `_KeReadFile()` - Reads file data
- `_KeFileStat()` - Gets file info
- `_KeFileSetOffset()` - Seeks in file
- `_KeCloseFile()` - Closes file

**Memory Operations**:

- `_KeMemMap()` - Maps memory, storing object location
- `_KeMemMapDirty()` - Marks memory modified, effectively setting a dirty bit

**Process/Signal**:

- `_KeSetSignal()` - Registers signal handler
- `_KeProcessExit()` - Exits process

**Debug**:

- `_KePrint()` - logging to terminal