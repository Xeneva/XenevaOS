# XenevaOS Policy on AI-Assisted Contributions

**Status:** Proposed  
**Applies to:** All XenevaOS contributors, maintainers, and development teams  
**Scope:** Source code, patches, tests, documentation, build scripts, security analysis, and other project contributions

## 1. Purpose

AI tools can help contributors research, understand code, debug issues, write tests, and develop changes faster. They can also produce incorrect, insecure, unsuitable, or poorly maintained output.

XenevaOS permits AI-assisted development, subject to transparent disclosure, appropriate verification, and human accountability. The goal is not to judge a contribution by the tool used, but to ensure that every accepted contribution meets the project's technical and licensing standards.

## 2. Core Principles

- **Human accountability:** The contributor is responsible for the entire submission, regardless of how much AI assistance was used.
- **Understanding:** Contributors must understand and be able to explain and defend the changes they submit. If they cannot, they must not submit those changes.
- **Disclosure:** Contributors must disclose substantial AI assistance in the format defined by the project.
- **Verification:** AI output must be checked through appropriate code review, testing, and analysis. An AI-generated explanation or test result is not proof of correctness by itself.
- **Risk-based review:** Review depth should reflect the component involved and the change's complexity, risk, and potential impact.
- **Human decisions:** AI tools must not independently approve, merge, or certify contributions.
- **Licensing and confidentiality:** Contributors must follow project licensing requirements and must not send secrets or confidential project information to unauthorized AI services.

## 3. Component Categories and Review

XenevaOS retains component categories to identify areas that need particular care. These categories guide review; they do not replace judgment about the individual change.

### Level 1 — Critical Low-Level Components

This category includes, but is not limited to:

- Bootloader, boot protocols, and early boot initialization
- Kernel core, interrupt handling, and exception handling
- Virtual memory, page tables, and physical memory management
- CPU context switching, thread scheduling, and synchronization primitives
- System calls, privilege transitions, and security enforcement
- Hardware drivers, including storage, networking, USB, GPU, and interrupt-controller drivers
- Filesystems, virtual filesystems, and mount management
- Kernel IPC, capability enforcement, and security-critical interfaces

**Policy:**

- **Strict Restrictions on Code Generation:** AI assistance is strictly restricted in Level 1 components. While AI tools may be used for preliminary research, reading comprehension, and conceptual study, delegating the creation, modification, or patch generation of critical code to AI is strictly restricted.
- **Mandatory Supervision and Authorship:** Any AI-assisted investigation or drafting must remain under close, continuous human supervision. Contributors must personally understand, explain, and defend every submitted line of logic; passing unverified or directly generated AI patches into this layer is strictly prohibited.
- **Rigorous Verification & Core Review:** AI-informed modifications must undergo thorough human validation, including regression testing, hardware, virtualization, and emulator stress testing, and static analysis where applicable, followed by mandatory in-depth review by a qualified core maintainer.
- **Mandatory Disclosure:** Any AI research or drafting assistance must be fully disclosed alongside tangible proof of manual verification. Maintainers may require additional stress tests, architectural reviews, or reject patches that lack sufficient human oversight.

### Level 2 — System Services and Core Infrastructure

This category includes, but is not limited to:

- User-space runtime and ABI
- System daemons and IPC services
- Graphics composition, display infrastructure, and critical graphical operations
- Dynamic loaders, executable loading, and system libraries
- Device-management daemons and other core user-space services

**Policy:**

- AI assistance is permitted, provided contributors remain responsible for the correctness and quality of their changes.
- Contributors must understand, review, and verify AI-generated or AI-modified code before submission.
- Substantial AI assistance must be disclosed, along with relevant verification evidence.
- Contributors must perform appropriate builds, tests, and static analysis where applicable.
- Changes affecting security boundaries, system stability, or critical infrastructure may require review comparable to Level 1.
- Maintainers may request additional evidence or testing when the complexity, risk, or impact of a change warrants it.

### Level 3 — Applications, Tools, and Documentation

This category includes, but is not limited to:

- Applications and user interfaces
- Developer utilities and build scripts
- Non-privileged applications and tools
- Documentation, examples, and tutorials
- Mock implementations and test harnesses

**Policy:**

- AI assistance is generally permitted for development, testing, debugging, and documentation.
- Contributors remain responsible for the correctness, maintainability, and quality of all submitted work.
- Contributors must review AI-generated content for errors, inappropriate assumptions, and consistency with XenevaOS coding standards.
- Substantial AI assistance must be disclosed, with verification evidence provided where applicable.
- Meaningful tests and appropriate validation must be performed for code changes.
- Changes with significant security, compatibility, or project-wide impact may require stricter review, regardless of their component category.

## 4. Review According to Risk and Impact

A component category alone does not determine how much review a change needs. Reviewers should also consider:

- **Complexity:** How difficult is the change to understand, reason about, and test?
- **Risk:** Could it introduce security vulnerabilities, data loss, memory corruption, instability, or compatibility problems?
- **Impact:** How widely could the change affect users, developers, build workflows, project governance, or future contributions?
- **Isolation and predictability:** Is the change narrowly scoped with behavior that can be tested reliably, or does it affect multiple subsystems?

A small documentation change can have broad impact if it changes contribution or licensing rules. Conversely, a narrowly scoped low-level change may be easier to reason about, though its critical nature still warrants careful review.

Reviewers may request additional tests, design notes, evidence, or independent review whenever the risk or impact warrants it. Changes to contribution rules, licensing guidance, security policy, or other project-wide requirements should receive review appropriate to their governance impact.

## 5. AI Disclosure

Contributors must disclose substantial AI assistance so reviewers can assess the submission with the right context. Disclosure is intended to support review, not to penalize AI usage.

The project may adopt a commit trailer such as:

`Assisted-by: LLM <tool name(s)>`

For example:

`Assisted-by: LLM ChatGPT, Claude`

The exact format should be standardized in `Contributing.md` and the pull request template. Contributors should also state, where practical, what the tool assisted with—for example, implementation, refactoring, test generation, debugging, or documentation.

Disclosure should be proportionate. Substantial generated or transformed content should be disclosed; trivial assistance such as spelling corrections need not require a separate attribution unless project rules say otherwise.

The project should clearly state whether the trailer is mandatory for substantial AI assistance. A consistent mandatory disclosure rule is preferable to an ambiguous optional convention.

## 6. Additional Evidence for AI-Assisted Pull Requests

For substantial AI-assisted changes, contributors should provide the following information in the pull request. Maintainers may request more evidence based on the change's risk.

- **AI usage:** Tools used and the parts of the change they assisted with.
- **Contributor explanation:** A short explanation of the design, important decisions, and behavior changed.
- **Verification performed:** Tests, builds, static analysis, manual checks, or other validation actually completed.
- **Results and limitations:** Relevant results, known gaps, untested cases, and remaining risks.
- **Change-specific considerations:** For complex or high-risk changes, explain important failure modes and how the change handles them.

Do not claim that a test, build, or analysis passed unless it was actually run and the result was checked. Full prompts or private AI conversations should not be required by default; they may contain confidential information and are not a substitute for technical evidence.

These artifacts help reviewers evaluate the change. They do not guarantee correctness or replace independent review.

## 7. Verification and Acceptance

Before submitting a contribution, the contributor must:

1. Review the complete diff, including AI-generated sections.
2. Understand the implementation and be able to explain its behavior and trade-offs.
3. Run relevant tests and checks, or clearly state what could not be run and why.
4. Check for unintended changes, unsafe assumptions, licensing issues, and exposed secrets.
5. Provide the required AI disclosure and verification details.

Maintainers may request changes, additional evidence, further testing, or rejection where the contribution does not meet XenevaOS standards. AI use alone does not make a contribution acceptable or unacceptable.

## 8. Confidentiality and Licensing

Contributors must not submit secrets, credentials, private keys, or confidential project information to AI services unless the service and use are explicitly authorized. Contributors must check AI-assisted output for licensing and attribution concerns and must not knowingly introduce incompatible or improperly copied material.

## 9. Project Documentation and Enforcement

The AI policy should be reflected in:

- `Contributing.md`, including disclosure rules and contributor responsibilities.
- The pull request template, including AI usage and verification fields.
- Review practices, including requests for stronger evidence on high-risk changes.

Maintainers should apply the policy consistently. The policy defines review expectations; it does not waive existing technical, security, licensing, or contribution requirements.

## Conclusion

XenevaOS permits AI-assisted development while requiring clear disclosure, meaningful verification, and human accountability. Contributors remain responsible for every submitted change. Review depth should reflect both the component involved and the complexity, risk, and impact of the specific change.

No contribution is entitled to acceptance solely because AI produced it, and no contribution should be rejected solely because AI was used. Acceptance depends on whether it meets XenevaOS's standards.
