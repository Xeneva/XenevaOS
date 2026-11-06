# Contributing to XenevaOS

Thank you for your interest in contributing to XenevaOS!

XenevaOS is a modern, lightweight operating system that focuses on performance, multimedia capabilities, and synchronization with current technologies. This guide outlines the contribution process and best practices to help maintain the quality and stability of the project.
## Getting Started
XenevaOS historically used Microsoft Visual Studio as the primary development platform on Windows. The maintained Linux compatibility workflow currently covers the AArch64 LLVM/Clang build; a separate AArch64 GCC path also remains in development.
- For Windows/MSVC: [You can click here for Build Instructions (Windows)](BuildInstructions.md).
- For AArch64 LLVM on Linux: [AArch64 LLVM/Clang Build on Linux](BuildInstructions(Linux).md).
- For AArch64 on Windows/MSYS2: [AArch64 Build Instructions for Windows](BuildInstructions(ARM64).md).

## Types of Contributions

### *Kernel Development*
- XenevaOS's kernel _'Aurora'_ is written purely in C and assembly. The kernel is the main component and a highly privileged layer of the operating system. Any incorrect code or untested changes can lead to system-wide crashes, instability, or security vulnerabilities.

- Always test kernel modifications in a controlled environment like QEMU or VirtualBox before pushing changes.

- Follow memory management, system calls, and other subsystem guidelines as outlined in the documentation. For details on virtual filesystem development, refer to the [Virtual File System Guide](Kernel/VirtualFileSystem.md).

- Before making major changes, open an issue or discussion in the repository or on Discord to align with the project's direction and avoid duplicate work.

### *Coding Style*
The XenevaOS project follows a consistent coding style to maintain readability and discipline. All contributions should adhere to this style to ensure uniformity across the codebase. Well-structured, clean, and documented code is highly valued, as it improves maintainability and collaboration. 

- Use _'PascalCase'_ for function naming and Structs. 
- If your contribution is for the Kernel, use PascalCase for function naming with the prefix __'Au'__, for example: ```AuPmmngrAlloc```
- Use _'snake_case'_ or _'PascalCase'_ for variable naming.
- Use __*lowercase*__ for C/C++ source and header files, and __*PascalCase*__ for folder names.
- Use __*PascalCase*__ for documentation (`.md`) files.
	- Keep functions modular and documented with comments.

### *Cross-Platform Compiler Compatibility*
To keep XenevaOS portable across MSVC, Clang/LLVM, and GCC environments, all contributors must adhere to strict C/C++ ISO standards.
- **Explicit Headers:** Do not rely on implicit headers. Always explicitly include standard headers (e.g., `#include <stddef.h>` for `size_t` and `#include <stdbool.h>` for `bool` in C).
- **Compiler-Provided Definitions:** Prefer using compiler-provided or standard-library-provided macro definitions (such as standard constants like `SIZE_MAX` or NEON/SIMD built-ins) where available. However, when project-specific or custom definitions are required for target platforms or freestanding environments (such as UEFI bootloader paths on MSVC), define them defensively using `#ifndef` guards to prevent macro redefinition conflicts under GCC.
- **Include Path Formatting (Forward Slashes):** Never use Windows-style backslashes (`\`) in include paths. Always use forward slashes (`/`) for cross-platform compatibility (e.g., `#include "BaseHdr/aurora.h"`).
- **Case-Sensitive Include Paths:** Unlike the usual Windows/MSVC environment, Linux toolchains use a case-sensitive filesystem. The casing in your `#include` statement must exactly match the actual folder and file names (e.g., use `Fs/` instead of `fs/`).
- **Variadic Arguments (`va_list`):** Never use manual pointer arithmetic to parse variadic arguments. Always use the standard `<stdarg.h>` macros (`va_start`, `va_arg`, `va_end`) to preserve AAPCS64 and compiler compatibility.
- **Strict Data Typing:** Clang and GCC diagnose many conversions that MSVC historically accepted. Avoid depending on compiler-specific quirks.
- **Variable Shadowing:** Do not name a structure member the same as a `typedef` in the same scope, as GCC strictly rejects this (`-Wchanges-meaning`).
- **Standard Syntax:** Avoid MSVC-specific calling conventions like `__cdecl` directly in source code; use custom macros if needed. Ensure scope resolutions are standard (e.g., `operator new` instead of `::operator new`).
- **Strict Returns:** Every non-void function must explicitly return a value. Missing returns result in undefined behavior under GCC.
- **Pragma Packing:** Always mathematically balance `#pragma pack(push, 1)` with `#pragma pack(pop)`.
- **Export Macros:** Do not hardcode `__declspec(dllexport)`. Always use cross-platform macros (e.g., `AU_EXPORT` and `AU_IMPORT`).
- **Minimal Fixes & TODO Offloading:** When fixing GCC/LLVM compatibility issues, apply only the strict minimum changes required to successfully compile and run the code without panics. Do not implement optional safeguards or heavy defensive guardrails immediately, as this expands the diff and risks breaking MSVC compatibility. Instead, defer these improvements by leaving a clear, capitalized `// TODO:` comment. This keeps PRs small and offloads non-critical enhancements to future contributors.

Code modifications must be verified for GCC compatibility before being merged.

### *AI-Assisted Contributions*
XenevaOS permits AI-assisted development (such as LLMs and automated tools), subject to transparent disclosure, human verification, and accountability:
- **Human Accountability:** The human contributor is solely responsible for the entire submission. Contributors must understand, explain, and defend all submitted code and changes. Never submit unverified AI output.
- **Mandatory Disclosure:** Any substantial AI assistance must be disclosed in the pull request description and attributed in git commit messages using the standardized trailer:
  ```text
  Assisted-by: LLM <tool name(s)>
  ```
  *(Example: `Assisted-by: LLM ChatGPT, Claude`)*
- **Tiered Component Verification:** 
  - **Level 1 (Critical Low-Level):** Boot, kernel, memory, interrupts, drivers, and VFS. AI usage is strictly restricted: permitted solely for research and comprehension; direct AI code creation, modification, or patch generation is prohibited without close human supervision and strict maintainer review.
  - **Level 2 (System Services):** Runtimes, IPC, graphics composition, and dynamic loaders require verified builds, tests, and security scrutiny.
  - **Level 3 (Applications, Tools & Docs):** Permitted with review for correctness, formatting, and conventions.
- For full guidelines and review criteria, refer to the [Policy on AI-Assisted Contributions](AIPolicy.md).

### *Bug Reports & Feature Requests*
- Open an issue to report bugs or suggest new features using the provided GitHub Issue templates.
- When reporting a bug, provide clear details about the issue, including system debug messages from the serial terminal, the processor state, and register values. If possible, describe the conditions under which the bug occurs.
- Mention your testing environment.
- For feature requests, explain the purpose and potential benefits to the project.

## Pull Request (PR) Guidelines
To ensure code quality and maintainability, all contributors must format their Pull Requests using the repository's standardized layout. A template is automatically provided on GitHub when opening a PR, and requires:
1. **Summary:** A concise explanation of the PR's high-level objectives.
2. **Changes:** A detailed bulleted list of modified modules, specifying exactly **Before** (previous behavior or issue), **What** changed, and **Why** it was changed.
3. **Compatibility:** Verification checkboxes indicating that the changes do not break MSVC/Windows and GCC/Linux builds or cause runtime regressions.
4. **AI Assistance:** Mandatory disclosure of any AI assistance utilized, tools used, and confirmation that the contributor understands and has verified the changes.
5. **Related:** Reference to any corresponding issue numbers (e.g., `Part of #44`).

### *Conclusion*
Thank you for your interest in contributing to XenevaOS! Your contributions — whether in code, documentation, bug reports, or discussions — help shape the future of this project. If you have any questions or need guidance, feel free to open an issue or join the discussions on GitHub or Discord. Let's work together to make a powerful and efficient operating system!
