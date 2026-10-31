/**
* BSD 2-Clause License
*
* Copyright (c) 2022 - 2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

/*
 * MSVC / Clang C++ ABI runtime stubs for the freestanding Windows PE target.
 *
 * Clang targeting the Windows COFF (MSVC) ABI references a handful of
 * C++ exception-handling / terminate symbols from object files that carry
 * exception tables or noexcept-terminated calls. XenevaOS userspace is
 * freestanding (no libstdc++/libcxx) and does not exercise C++ exceptions,
 * so these symbols are provided as minimal stubs so the static archive links
 * cleanly. They are only emitted under Clang; the GCC build is left untouched.
 */

#if defined(__clang__)
extern "C" {

/* __CxxFrameHandler3 -- C++ exception frame handler (MSVC EH ABI).
 * Only invoked by the OS during real exception unwinding, which never
 * happens in the freestanding no-throw runtime. */
int __CxxFrameHandler3(void) {
	return 0;
}

/* __std_terminate -- called when a noexcept boundary is violated or the
 * runtime decides to terminate. Halt the core. */
void __std_terminate(void) {
	for (;;) {
		__asm__ volatile("wfi");
	}
}

} /* extern "C" */
#endif
