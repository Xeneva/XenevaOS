# XenevaOS Policy on the Use of Artificial Intelligence in Development

***Status***: Proposed <br>
***Applies to***: All XenevaOS contributors, new comers, maintainers, and development teams<br>
***Scope***: Source code, patches, tests, documentation, security analysis, and other project contributions

## 1. Purpose
XenevaOS recognizes that artificial intelligence (AI) and AI-assisted development tools can improve developer productivity, accelerate debugging, assist with research, and support software testing.

However, an operating system is a security-critical software system in which defects can compromise memory safety, hardware isolation, system integrity, reliability, and the execution of applications.

XenevaOS therefore approaches a proportional practice for level of supervision and review with the criticality of the component being modified or written using AI-assisted tools.

***AI tools may assist development, but it is to the human contributor for reviewing the correctness, security, licensing, and maintainability of every contribution.***

## 2. Core Principles
All AI-assisted contributions to XenevaOS must follow these principles:

- **Human accountability** : AI-generated code must never be treated as inherently correct or trustworthy. The submitting developer is responsible for entire contribution.
- **Compulsory Review:** AI-generated code must be reviewed by human before submission. Lead Maintainer or suitable maintainer should check for _security, data structures or the way data structures are used, critical logic being used, memory management,null-pointer check, and code descipline_. Critical components require enhanced review by a suitably qualified maintainer.
- **Risk-based restrictions:** The use of AI shall be more tightly controlled in components that interact directly with hardware, manage memory, handle privileged execution, or enforce security boundaries.
- **Transparency:** Contributors must mention properly about substantial AI assistance so reviewers can evaluate the contribution appropriately.
- **Verification over confidence:** AI-generated explantions, code, comment and proposed fixes must be independently verified through appropriate analysis and testing.
- **No automated approval:** AI tools must not independently approve, merge, or certify contributions on behalf of a human maintainer.
- **Licensing and confidentiality:** Contributors must ensure that AI use does not introduce incompatible licensing conflicts, improperly reproduced code, or disclosure of internal and confidential information.

## 3. Classification of Components

XenevaOS classifies components into three review levels according to their potential impact on system security and stability.

### Level 1 - Critical Low-Level Components
This category includes, but is not limited to:
- Bootloader, boot protocols, and early boot initialization
- Kernel core, interrupt handling, and exception handling
- Virtual memory management, page tables, and physical memory management.
- CPU context switching, thread scheduling, and synchronization primitives.
- System calls, privilege transitions, and security enforcement
- Hardware drivers, including storage, networking, USB, GPU, and interrupt-controller drivers. 
- File system and Virtual file system, Mount points and multiple file system management.
- Kernel IPC, capability enforcement, and other security-critical interfaces.

***Policy:*** AI use in Level 1 components is restricted and must be closely reviewed and tested.

AI may assist with research, design exploration, code explanation, test-case generation, static-analysis interpretation, and proposed implementations. However, AI-generated or AI-substantially modified code must not be submitted without proper human verification, stress-test and the required review.

### Level 2 - System Services and Core Infrastructure

This category includes:
- User-space runtime and ABI
- System daemons and IPC services
- Graphics composition and display infrastructure, critical graphical maths
- Dynamic loaders, executable loading in user space, and system libraries used in user space
- Device management daemons and other user space components

***Policy:*** AI assistance is permitted, subject to human review, appropriate stress-testing, and disclosure of substantial generated content.

### Level 3 - Applications, Tools, and Documentation

This category includes:
- Applications and user interfaces
- Developer utilities and build scripts
- Non-priviledged applications and files
- Documentation, examples, and tutorials
- Mock implementations and development tooling and Test harnesses

***Policy:*** AI assistance is generally permitted.

Contributors must still verify generated output, follow project coding standard and Xeneva defined coding styles, respect licensing requirements, and ensure that tests are meaningful. 

## Conclusion
XenevaOS aims to build a reliable, secure, and maintainable operating system through engineering discipline and transparent collaboration.

Xeneva encourages use of AI tools in a certain level. The more critical a component is, the greater the requirement for human understanding, supervision, verification, and accountability.

***No AI-generated contribution is entitled to acceptance without meeting the technical standards of XenevaOS.***

