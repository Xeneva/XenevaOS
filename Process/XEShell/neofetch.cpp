/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/_ketime.h>
#include <stdio.h>

/**
 * ASCII art generated using LLM : Claude Sonnet 5.5 (Medium)
 */

#define NF_ART_ROWS 14
#define NF_ART_COLS 42 /* 40 art + 2 gap */

static const char* nf_art[NF_ART_ROWS] = {
	"##%:",
	" .###%:",
	"   .####%%:",
	"  :#:   .####%%:",
	" .:     .####%%%%%:",
	"   :####%     .###%%%%:",
	"    .:####%%%%%%%%%%######:    .#####%:",
	"     ,            %%%%%###: :####  ###%:",
	"       .:###%%%%%%%%%%%####: .#####%%:",
	"           .::#####%%%%%####: :####:",
	"            .:#####%%%% %####: :#:",
	"                         .###: .",
	"                          :##:",
	"                          .:",
};

/* Dark blue top-left, cyan bottom-right; head stays blue like the logo.
 * Only single-parameter SGR (ESC[34m / ESC[36m) because that's all
 * ProcessControlSequence parses. Generated using Claude Sonnet 5.5 Medium*/
static int NfColor(int r, int c) {
	if (c >= 28 && r <= 8)
		return 34;
	return (r * 3 + c) < 30 ? 34 : 36;
}

static void NfArtRow(int r) {
	const char* s = (r >= 0 && r < NF_ART_ROWS) ? nf_art[r] : "";
	int len = (int)strlen(s);
	int cur = 0;
	for (int c = 0; c < NF_ART_COLS; c++) {
		char ch = (c < len) ? s[c] : ' ';
		if (ch != ' ') {
			int col = NfColor(r, c);
			if (col != cur) {
				printf("\033[%dm", col);
				cur = col;
			}
		}
		printf("%c", ch);
	}
	printf("\033[39m");
}

static void NfLine(int r, const char* label, const char* value) {
	NfArtRow(r);
	if (label)
		printf("\033[33m%s\033[39m%s", label, value);
	printf("\n"); /* terminal resets colours on \n, so every row re-emits them */
}

void XEShellNeofetch() {
	char up[32];
	uint64_t s = _KeGetCurrentMS() / 1000; /* verify: ms since boot? */
	snprintf(up, sizeof(up), "%dh %dm %ds", (int)(s / 3600), (int)((s / 60) % 60), (int)(s % 60));

	const char* arch =
#ifdef ARCH_ARM64
		"ARMv8-A (AArch64)";
#else
		"x86_64";
#endif

	struct utsname name;
	memset(&name, 0, sizeof(utsname));
	if (uname(&name)) {
		printf("failed to get system information \r\n");
		return;
	}

	printf("\n");
	NfLine(0, NULL, "");
	NfLine(1, "OS:       ", name.sysname);
	NfLine(2, "Kernel:   ", "Aurora Bordoisila");
	NfLine(3, "Arch:     ", name.machine);
	NfLine(4, "Uptime:   ", up);
	NfLine(5, "Shell:    ", "xesh v1.1");
	NfLine(6, "Terminal: ", "Xeneva Terminal");
	NfLine(7, "WM:       ", "DeodhaiXR");
	NfLine(8, NULL, "");

	/* colour palette strip using the bg codes your terminal supports */
	NfArtRow(9);
	for (int c = 40; c <= 47; c++)
		printf("\033[%dm   ", c);
	printf("\033[49m\n");

	for (int r = 10; r < NF_ART_ROWS; r++)
		NfLine(r, NULL, "");
	printf("\n");
	fflush(stdout);
}