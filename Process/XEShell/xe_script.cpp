/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

/**
 * NOTE : GENERATED USING CLAUDE Sonnet 5.5 (Medium)
 */

#include "xe_script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* limits (tune these to the memory you can spare)                     */
/* ------------------------------------------------------------------ */
#define XS_VAR_SLOTS  256 /* power of two */
#define XS_FUNC_SLOTS 128 /* power of two */
#define XS_NAME_MAX	  32
#define XS_VAL_MAX	  256
#define XS_FIELD_MAX  1024
#define XS_SCRATCH	  (64 * 1024)
#define XS_MAX_FRAMES 32
#define XS_MAX_DEPTH  64
#define XS_MAX_SCRIPT (1 << 20)
#define XS_MAX_NEST	  8
#define XS_FD_NULL	  (-2)

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
static inline bool IsDig(char c) {
	return c >= '0' && c <= '9';
}
static inline bool IsAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline bool IsIdS(char c) {
	return IsAlpha(c) || c == '_';
}
static inline bool IsIdC(char c) {
	return IsIdS(c) || IsDig(c);
}
static inline bool IsSp(char c) {
	return c == ' ' || c == '\t' || c == '\r';
}
static inline char Lc(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}
static inline char Uc(char c) {
	return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static uint32_t Fnv(const char* s) {
	uint32_t h = 2166136261u;
	while (*s) {
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}
	return h;
}

static bool EqCI(const char* a, const char* b) {
	while (*a && *b) {
		if (Lc(*a) != Lc(*b))
			return false;
		a++;
		b++;
	}
	return *a == *b;
}

/* does s start with keyword kw (kw is lower case) followed by end/space/( ? */
static bool StartsKw(const char* s, const char* kw) {
	while (*kw) {
		if (Lc(*s) != *kw)
			return false;
		s++;
		kw++;
	}
	return *s == 0 || IsSp(*s) || *s == '(' || *s == '.';
}

static char* TrimL(char* s) {
	while (IsSp(*s))
		s++;
	return s;
}

static int HexVal(char c) {
	if (IsDig(c))
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int64_t ParseI64(const char* s, bool* ok) {
	int64_t v = 0;
	bool neg = false;
	bool any = false;
	while (IsSp(*s) || *s == '\n')
		s++;
	if (*s == '-') {
		neg = true;
		s++;
	} else if (*s == '+')
		s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		for (; HexVal(*s) >= 0; s++) {
			v = v * 16 + HexVal(*s);
			any = true;
		}
	} else {
		for (; IsDig(*s); s++) {
			v = v * 10 + (*s - '0');
			any = true;
		}
	}
	while (IsSp(*s) || *s == '\n')
		s++;
	if (ok)
		*ok = any && *s == 0;
	return neg ? -v : v;
}

static int FmtI64(int64_t v, char* out) {
	char tmp[24];
	int n = 0, o = 0;
	uint64_t u;
	if (v < 0) {
		out[o++] = '-';
		u = (uint64_t)(-(v + 1)) + 1u;
	} else
		u = (uint64_t)v;
	do {
		tmp[n++] = (char)('0' + (u % 10));
		u /= 10;
	} while (u);
	while (n)
		out[o++] = tmp[--n];
	out[o] = 0;
	return o;
}

static void Err(int line, const char* msg) {
	printf("[xesh]: line %d: %s\r\n", line, msg);
}

/* Decode one backslash escape. *ps points at the character AFTER the
 * backslash on entry, and at the LAST character consumed on return. */
static int EscSeq(const char** ps) {
	const char* s = *ps;
	int v;
	switch (*s) {
	case 'n':
		v = 10;
		break;
	case 't':
		v = 9;
		break;
	case 'r':
		v = 13;
		break;
	case 'e':
	case 'E':
		v = 27;
		break;
	case 'a':
		v = 7;
		break;
	case 'b':
		v = 8;
		break;
	case 'f':
		v = 12;
		break;
	case 'v':
		v = 11;
		break;
	case 'x': {
		int h = HexVal(s[1]);
		if (h < 0) {
			v = 'x';
			break;
		}
		v = h;
		s++;
		if (HexVal(s[1]) >= 0) {
			v = v * 16 + HexVal(s[1]);
			s++;
		}
		break;
	}
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7': {
		int maxMore = (*s == '0') ? 3 : 2;
		v = (*s == '0') ? 0 : (*s - '0');
		while (maxMore-- > 0 && s[1] >= '0' && s[1] <= '7') {
			v = v * 8 + (s[1] - '0');
			s++;
		}
		break;
	}
	default:
		v = (unsigned char)*s;
		break;
	}
	*ps = s;
	return v & 0xFF;
}

/* ------------------------------------------------------------------ */
/* arena                                                               */
/* ------------------------------------------------------------------ */
typedef struct XsBlock {
	struct XsBlock* next;
	size_t cap, used;
} XsBlock;
typedef struct XsArena {
	XsBlock* head;
} XsArena;

static void* ArenaAlloc(XsArena* a, size_t n) {
	n = (n + 7) & ~(size_t)7;
	XsBlock* b = a->head;
	if (!b || b->used + n > b->cap) {
		size_t cap = n > 8192 ? n : 8192;
		b = (XsBlock*)malloc(sizeof(XsBlock) + cap);
		if (!b)
			return NULL;
		b->cap = cap;
		b->used = 0;
		b->next = a->head;
		a->head = b;
	}
	void* p = (char*)(b + 1) + b->used;
	b->used += n;
	return p;
}

static char* ArenaStr(XsArena* a, const char* s, int len) {
	char* p = (char*)ArenaAlloc(a, (size_t)len + 1);
	if (!p)
		return NULL;
	memcpy(p, (void*)s, (size_t)len);
	p[len] = 0;
	return p;
}

static void ArenaFree(XsArena* a) {
	XsBlock* b = a->head;
	while (b) {
		XsBlock* n = b->next;
		free(b);
		b = n;
	}
	a->head = NULL;
}

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */
enum {
	N_CMD = 1,
	N_AND,
	N_OR,
	N_PIPE,
	N_NOT,
	N_IF,
	N_WHILE,
	N_FOR,
	N_CASE,
	N_FUNC,
	N_GROUP,
	N_BCMD,
	N_BIF,
	N_BFOR,
	N_GOTO,
	N_SELECT
};
enum { R_IN, R_OUT, R_APP, R_DUP };
enum { BK_EXIST = 1, BK_DEF, BK_ERR, BK_CMP };
enum { CMP_EQ = 1, CMP_NE, CMP_LT, CMP_LE, CMP_GT, CMP_GE };

typedef struct Redir {
	struct Redir* next;
	int fd, op;
	char* target;
	bool raw;
} Redir;

struct Node;
typedef struct CaseItem {
	char** pats;
	int np;
	struct Node* body;
} CaseItem;

typedef struct Node {
	uint8_t type, flag;
	int line;
	struct Node *a, *b, *c, *next;
	char** w;
	int nw;
	Redir* redir;
	char* s;
	int n, m;
	CaseItem* items;
} Node;

typedef struct {
	char* name;
	int idx;
} Label;
typedef struct {
	Node** st;
	int nst;
	Label* tab;
	int tabMask;
} Prog;

/* ------------------------------------------------------------------ */
/* interpreter state                                                   */
/* ------------------------------------------------------------------ */
typedef struct HHdr {
	uint32_t h;
	uint8_t used;
} HHdr;

typedef struct {
	HHdr hd;
	uint8_t exported;
	char name[XS_NAME_MAX];
	char val[XS_VAL_MAX];
} Var;
typedef struct {
	HHdr hd;
	char name[XS_NAME_MAX];
	Node* body;
	void* owner;
} Func;

static Var g_vars[XS_VAR_SLOTS];
static int g_nvars;
static Func g_funcs[XS_FUNC_SLOTS];
static int g_nfuncs;

typedef struct {
	int argc;
	char** argv;
	int shift;
	char** own;
	int nown;
} Frame;
static Frame g_frames[XS_MAX_FRAMES];
static int g_fp = -1;

enum { FL_NONE = 0, FL_BREAK, FL_CONT, FL_RETURN, FL_EXIT, FL_GOTO };
static int g_flow, g_flow_n;
static int g_status;
static int g_depth, g_loopdepth, g_scriptdepth, g_substdepth, g_capdepth, g_pipedepth;
static int g_fd_in = -1, g_fd_out = -1, g_fd_err = -1;
static bool g_bat_mode;
static bool g_bat_echo = true;
static Prog* g_prog;
static XsArena* g_cur_arena;

static char g_scratch[XS_SCRATCH];
static size_t g_stop;

static void* ScratchAlloc(size_t n) {
	n = (n + 7) & ~(size_t)7;
	if (g_stop + n > XS_SCRATCH)
		return NULL;
	void* p = g_scratch + g_stop;
	g_stop += n;
	return p;
}

/* backward-shift deletion for open-addressing tables whose slots begin with HHdr */
static void HDel(void* base, size_t stride, int mask, int i) {
	char* b = (char*)base;
	int j = i;
	for (;;) {
		((HHdr*)(b + (size_t)i * stride))->used = 0;
		for (;;) {
			j = (j + 1) & mask;
			HHdr* hj = (HHdr*)(b + (size_t)j * stride);
			if (!hj->used)
				return;
			int k = (int)(hj->h & (uint32_t)mask);
			bool stay = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
			if (!stay)
				break;
		}
		memcpy(b + (size_t)i * stride, b + (size_t)j * stride, stride);
		i = j;
	}
}

/* ---- variables ---- */
static bool NormName(const char* in, char* out) {
	size_t i = 0;
	for (; in[i]; i++) {
		if (i >= XS_NAME_MAX - 1)
			return false;
		out[i] = g_bat_mode ? Uc(in[i]) : in[i];
	}
	out[i] = 0;
	return i > 0;
}

static Var* VarSlot(const char* rawName, bool create) {
	char name[XS_NAME_MAX];
	if (!NormName(rawName, name))
		return NULL;
	uint32_t h = Fnv(name);
	int i = (int)(h & (XS_VAR_SLOTS - 1));
	while (g_vars[i].hd.used) {
		if (g_vars[i].hd.h == h && strcmp(g_vars[i].name, name) == 0)
			return &g_vars[i];
		i = (i + 1) & (XS_VAR_SLOTS - 1);
	}
	if (!create || g_nvars >= XS_VAR_SLOTS * 3 / 4)
		return NULL;
	Var* v = &g_vars[i];
	memset(v, 0, sizeof(*v));
	v->hd.used = 1;
	v->hd.h = h;
	strcpy(v->name, name);
	g_nvars++;
	return v;
}

static const char* VarGet(const char* name) {
	Var* v = VarSlot(name, false);
	if (v)
		return v->val;
	if (strcmp(name, "PWD") == 0)
		return XsHostCwd();
	return NULL;
}

static bool VarSet(const char* name, const char* val) {
	Var* v = VarSlot(name, true);
	if (!v) {
		printf("[xesh]: variable table full or bad name: %s\r\n", name);
		return false;
	}
	strncpy(v->val, val, XS_VAL_MAX - 1);
	v->val[XS_VAL_MAX - 1] = 0;
	if (v->exported)
		XsHostSetEnv(v->name, v->val);
	return true;
}

static void VarUnset(const char* rawName) {
	char name[XS_NAME_MAX];
	if (!NormName(rawName, name))
		return;
	uint32_t h = Fnv(name);
	int i = (int)(h & (XS_VAR_SLOTS - 1));
	while (g_vars[i].hd.used) {
		if (g_vars[i].hd.h == h && strcmp(g_vars[i].name, name) == 0) {
			HDel(g_vars, sizeof(Var), XS_VAR_SLOTS - 1, i);
			g_nvars--;
			return;
		}
		i = (i + 1) & (XS_VAR_SLOTS - 1);
	}
}

/* ---- functions ---- */
static Func* FuncFind(const char* name) {
	if (g_nfuncs == 0 || strlen(name) >= XS_NAME_MAX)
		return NULL;
	uint32_t h = Fnv(name);
	int i = (int)(h & (XS_FUNC_SLOTS - 1));
	while (g_funcs[i].hd.used) {
		if (g_funcs[i].hd.h == h && strcmp(g_funcs[i].name, name) == 0)
			return &g_funcs[i];
		i = (i + 1) & (XS_FUNC_SLOTS - 1);
	}
	return NULL;
}

static void FuncSet(const char* name, Node* body, void* owner) {
	if (strlen(name) >= XS_NAME_MAX)
		return;
	Func* f = FuncFind(name);
	if (!f) {
		if (g_nfuncs >= XS_FUNC_SLOTS * 3 / 4) {
			printf("[xesh]: function table full\r\n");
			return;
		}
		uint32_t h = Fnv(name);
		int i = (int)(h & (XS_FUNC_SLOTS - 1));
		while (g_funcs[i].hd.used)
			i = (i + 1) & (XS_FUNC_SLOTS - 1);
		f = &g_funcs[i];
		memset(f, 0, sizeof(*f));
		f->hd.used = 1;
		f->hd.h = h;
		strcpy(f->name, name);
		g_nfuncs++;
	}
	f->body = body;
	f->owner = owner;
}

/* drop every function that lives in an arena that is about to be freed */
static void FuncPurge(void* owner) {
	if (g_nfuncs == 0)
		return;
	for (int i = 0; i < XS_FUNC_SLOTS;) {
		if (g_funcs[i].hd.used && g_funcs[i].owner == owner) {
			HDel(g_funcs, sizeof(Func), XS_FUNC_SLOTS - 1, i);
			g_nfuncs--;
		} else
			i++;
	}
}

/* ---- positional parameters ---- */
static const char* PosArg(int n) {
	if (g_fp < 0)
		return NULL;
	Frame* f = &g_frames[g_fp];
	if (n == 0)
		return f->argc > 0 ? f->argv[0] : "";
	int idx = n + f->shift;
	return idx < f->argc ? f->argv[idx] : NULL;
}
static int PosCount(void) {
	if (g_fp < 0)
		return 0;
	Frame* f = &g_frames[g_fp];
	int n = f->argc - 1 - f->shift;
	return n < 0 ? 0 : n;
}
static void FrameFreeOwn(Frame* f) {
	if (f->own) {
		for (int i = 1; i < f->nown; i++)
			free(f->own[i]);
		free(f->own);
	}
	f->own = NULL;
	f->nown = 0;
}

/* ------------------------------------------------------------------ */
/* I/O helpers                                                         */
/* ------------------------------------------------------------------ */
static void XsWrite(int fd, const char* s, int n) {
	if (n <= 0 || fd == XS_FD_NULL)
		return;
	if (fd >= 0) {
		XsHostWrite(fd, s, n);
		return;
	}
	char buf[256];
	while (n > 0) {
		int c = n < 255 ? n : 255;
		memcpy(buf, (void*)s, (size_t)c);
		buf[c] = 0;
		printf("%s", buf);
		s += c;
		n -= c;
	}
	fflush(stdout);
}
static void XsPuts(int fd, const char* s) {
	XsWrite(fd, s, (int)strlen(s));
}

/* tiny output buffer so printf/echo cost one write per call, not per byte */
typedef struct {
	char b[256];
	int n;
	int fd;
} Ob;
static void ObFlush(Ob* o) {
	if (o->n) {
		XsWrite(o->fd, o->b, o->n);
		o->n = 0;
	}
}
static void ObPut(Ob* o, char c) {
	if (o->n == (int)sizeof o->b)
		ObFlush(o);
	o->b[o->n++] = c;
}
static void ObStr(Ob* o, const char* s) {
	while (*s)
		ObPut(o, *s++);
}

/* small read-ahead cache so `while read line; do ... done < file` is not one
 * syscall per byte. 4 fds at a time, linear lookup of 4 = O(1). */
typedef struct {
	int fd;
	int pos, len;
	char buf[256];
} RbSlot;
static RbSlot g_rb[4] = {{-3, 0, 0, {0}}, {-3, 0, 0, {0}}, {-3, 0, 0, {0}}, {-3, 0, 0, {0}}};
static int g_rb_next;

static void RbInvalidate(int fd) {
	for (int i = 0; i < 4; i++)
		if (g_rb[i].fd == fd)
			g_rb[i].fd = -3;
}
static int ReadByte(int fd) {
	if (fd == XS_FD_NULL)
		return -1;
	RbSlot* s = NULL;
	for (int i = 0; i < 4; i++)
		if (g_rb[i].fd == fd) {
			s = &g_rb[i];
			break;
		}
	if (!s) {
		s = &g_rb[g_rb_next++ & 3];
		s->fd = fd;
		s->pos = s->len = 0;
	}
	if (s->pos >= s->len) {
		int n = XsHostRead(fd, s->buf, (int)sizeof(s->buf));
		if (n <= 0)
			return -1;
		s->len = n;
		s->pos = 0;
	}
	return (unsigned char)s->buf[s->pos++];
}

static inline bool StdinIsConsole(void) {
	return g_fd_in < 0 && g_fd_in != XS_FD_NULL;
}

/* one raw byte from the current stdin: console -> unbuffered key, else file */
static int ReadKeyIn(void) {
	if (StdinIsConsole())
		return XsHostReadKey();
	return ReadByte(g_fd_in);
}

/* read one line from the current stdin; false on EOF with nothing read */
static bool ReadLineIn(char* out, int max) {
	if (StdinIsConsole()) {
		int n = XsHostReadLine(out, max);
		return n >= 0;
	}
	int n = 0, c;
	bool got = false;
	while ((c = ReadByte(g_fd_in)) >= 0) {
		got = true;
		if (c == '\n')
			break;
		if (c != '\r' && n < max - 1)
			out[n++] = (char)c;
	}
	out[n] = 0;
	return got;
}

/* ------------------------------------------------------------------ */
/* glob matching (case, ${x%pat}); '\' escapes the next character       */
/* ------------------------------------------------------------------ */
static bool GlobMatch(const char* p, const char* s) {
	const char *star = NULL, *ss = s;
	while (*s) {
		if (*p == '*') {
			star = p++;
			ss = s;
		} else if (*p == '?') {
			p++;
			s++;
		} else if (*p == '[') {
			const char* q = p + 1;
			bool neg = false, hit = false;
			if (*q == '!' || *q == '^') {
				neg = true;
				q++;
			}
			const char* first = q;
			while (*q && (*q != ']' || q == first)) {
				if (q[1] == '-' && q[2] && q[2] != ']') {
					if (*s >= q[0] && *s <= q[2])
						hit = true;
					q += 3;
				} else {
					if (*s == *q)
						hit = true;
					q++;
				}
			}
			if (*q == ']') {
				if (hit != neg) {
					p = q + 1;
					s++;
				} else if (star) {
					p = star + 1;
					s = ++ss;
				} else
					return false;
			} else { /* no closing ']': '[' is an ordinary character */
				if (*s == '[') {
					p++;
					s++;
				} else if (star) {
					p = star + 1;
					s = ++ss;
				} else
					return false;
			}
		} else if (*p == '\\' && p[1]) {
			if (p[1] == *s) {
				p += 2;
				s++;
			} else if (star) {
				p = star + 1;
				s = ++ss;
			} else
				return false;
		} else if (*p == *s) {
			p++;
			s++;
		} else if (star) {
			p = star + 1;
			s = ++ss;
		} else
			return false;
	}
	while (*p == '*')
		p++;
	return *p == 0;
}

/* ------------------------------------------------------------------ */
/* arithmetic: precedence climbing, int64                              */
/* ------------------------------------------------------------------ */
typedef struct {
	const char* p;
	bool err;
} Ar;
static int64_t ArExpr(Ar* a, int minp);

static void ArWs(Ar* a) {
	while (IsSp(*a->p) || *a->p == '\n')
		a->p++;
}

static int64_t ArVal(const char* name) {
	const char* v = VarGet(name);
	if (!v || !*v)
		return 0;
	return ParseI64(v, NULL);
}

static void ArSet(const char* name, int64_t v) {
	char b[24];
	FmtI64(v, b);
	VarSet(name, b);
}

static int64_t ArUnary(Ar* a) {
	ArWs(a);
	char c = *a->p;
	if (c == '-') {
		a->p++;
		return -ArUnary(a);
	}
	if (c == '+') {
		a->p++;
		return ArUnary(a);
	}
	if (c == '!') {
		a->p++;
		return !ArUnary(a);
	}
	if (c == '~') {
		a->p++;
		return ~ArUnary(a);
	}
	if (c == '(') {
		a->p++;
		int64_t v = ArExpr(a, 0);
		ArWs(a);
		if (*a->p == ')')
			a->p++;
		else
			a->err = true;
		return v;
	}
	if (IsDig(c)) {
		int64_t v = 0;
		if (c == '0' && (a->p[1] == 'x' || a->p[1] == 'X')) {
			a->p += 2;
			for (; HexVal(*a->p) >= 0; a->p++)
				v = v * 16 + HexVal(*a->p);
		} else {
			while (IsDig(*a->p))
				v = v * 10 + (*a->p++ - '0');
		}
		return v;
	}
	if (IsIdS(c)) {
		char name[XS_NAME_MAX];
		int n = 0;
		while (IsIdC(*a->p)) {
			if (n < XS_NAME_MAX - 1)
				name[n++] = *a->p;
			a->p++;
		}
		name[n] = 0;
		ArWs(a);
		const char* p = a->p;
		if (p[0] == '+' && p[1] == '+') {
			a->p += 2;
			int64_t o = ArVal(name);
			ArSet(name, o + 1);
			return o;
		}
		if (p[0] == '-' && p[1] == '-') {
			a->p += 2;
			int64_t o = ArVal(name);
			ArSet(name, o - 1);
			return o;
		}
		if (p[0] == '=' && p[1] != '=') {
			a->p++;
			int64_t v = ArExpr(a, 0);
			ArSet(name, v);
			return v;
		}
		if ((p[0] == '+' || p[0] == '-' || p[0] == '*' || p[0] == '/' || p[0] == '%') &&
			p[1] == '=') {
			char op = p[0];
			a->p += 2;
			int64_t r = ArExpr(a, 0), o = ArVal(name), v = o;
			switch (op) {
			case '+':
				v = o + r;
				break;
			case '-':
				v = o - r;
				break;
			case '*':
				v = o * r;
				break;
			case '/':
				if (r == 0) {
					a->err = true;
					return 0;
				}
				v = o / r;
				break;
			case '%':
				if (r == 0) {
					a->err = true;
					return 0;
				}
				v = o % r;
				break;
			}
			ArSet(name, v);
			return v;
		}
		return ArVal(name);
	}
	a->err = true;
	return 0;
}

static int ArPrec(const char* p, int* len) {
	*len = 2;
	if (p[0] == '|' && p[1] == '|')
		return 1;
	if (p[0] == '&' && p[1] == '&')
		return 2;
	if (p[0] == '=' && p[1] == '=')
		return 6;
	if (p[0] == '!' && p[1] == '=')
		return 6;
	if (p[0] == '<' && p[1] == '=')
		return 7;
	if (p[0] == '>' && p[1] == '=')
		return 7;
	if (p[0] == '<' && p[1] == '<')
		return 8;
	if (p[0] == '>' && p[1] == '>')
		return 8;
	*len = 1;
	switch (p[0]) {
	case '|':
		return 3;
	case '^':
		return 4;
	case '&':
		return 5;
	case '<':
	case '>':
		return 7;
	case '+':
	case '-':
		return 9;
	case '*':
	case '/':
	case '%':
		return 10;
	}
	return 0;
}

static int64_t ArExpr(Ar* a, int minp) {
	int64_t l = ArUnary(a);
	for (;;) {
		ArWs(a);
		int len;
		int pr = ArPrec(a->p, &len);
		if (pr == 0 || pr < minp)
			break;
		/* an op= form belongs to the identifier on the left, not to us */
		if (len == 1 && a->p[1] == '=' && a->p[0] != '<' && a->p[0] != '>')
			break;
		char o0 = a->p[0], o1 = len == 2 ? a->p[1] : 0;
		a->p += len;
		int64_t r = ArExpr(a, pr + 1);
		switch (pr) {
		case 1:
			l = (l || r);
			break;
		case 2:
			l = (l && r);
			break;
		case 3:
			l = l | r;
			break;
		case 4:
			l = l ^ r;
			break;
		case 5:
			l = l & r;
			break;
		case 6:
			l = (o0 == '=') ? (l == r) : (l != r);
			break;
		case 7:
			if (o0 == '<')
				l = o1 ? (l <= r) : (l < r);
			else
				l = o1 ? (l >= r) : (l > r);
			break;
		case 8:
			l = (o0 == '<') ? (l << r) : (l >> r);
			break;
		case 9:
			l = (o0 == '+') ? (l + r) : (l - r);
			break;
		case 10:
			if (o0 == '*')
				l = l * r;
			else if (r == 0) {
				a->err = true;
				return 0;
			} else
				l = (o0 == '/') ? (l / r) : (l % r);
			break;
		}
	}
	return l;
}

static int64_t ArithEval(const char* s, bool* err) {
	Ar a = {s, false};
	int64_t v = ArExpr(&a, 0);
	ArWs(&a);
	if (*a.p)
		a.err = true;
	if (err)
		*err = a.err;
	return v;
}

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */
enum { T_EOF, T_NL, T_WORD, T_SEMI, T_DSEMI, T_AMP, T_AND, T_OR, T_PIPE, T_LP, T_RP, T_REDIR };

typedef struct Parser {
	const char* src;
	int pos, len, line;
	XsArena* ar;
	int tok;
	char* text;
	bool quoted;
	int rfd, rop;
	int tline;
	bool err;
	char* scratch[512];
	Label labs[256];
	int nlabs;
} Parser;

static Parser* ParserNew(const char* text, int len) {
	Parser* P = (Parser*)calloc(1, sizeof(Parser));
	if (!P)
		return NULL;
	P->src = text;
	P->len = len;
	P->line = 1;
	return P;
}

static Node* NewNode(Parser* P, int type) {
	Node* n = (Node*)ArenaAlloc(P->ar, sizeof(Node));
	if (!n) {
		P->err = true;
		static Node dummy;
		return &dummy;
	}
	memset(n, 0, sizeof(Node));
	n->type = (uint8_t)type;
	n->line = P->tline ? P->tline : P->line;
	return n;
}

static void PErr(Parser* P, const char* msg) {
	if (!P->err) {
		Err(P->tline ? P->tline : P->line, msg);
		/* show the text around the failure so the bad line is obvious */
		int e = P->pos > P->len ? P->len : P->pos;
		int b = e;
		while (b > 0 && P->src[b - 1] != '\n' && e - b < 60)
			b--;
		char frag[64];
		int n = 0;
		for (int i = b; i < e && n < 62; i++) {
			unsigned char c = (unsigned char)P->src[i];
			frag[n++] = (c < 32 || c > 126) ? '?' : (char)c;
		}
		frag[n] = 0;
		printf("[xesh]:   near: %s <--\r\n", frag);
	}
	P->err = true;
}

/* skip a $( ... ) group; src[pos] == '$' and src[pos+1] == '(' */
static void SkipParen(Parser* P) {
	const char* s = P->src;
	int depth = 1;
	P->pos += 2;
	while (P->pos < P->len && depth) {
		char c = s[P->pos];
		if (c == '\n')
			P->line++;
		if (c == '\'') {
			P->pos++;
			while (P->pos < P->len && s[P->pos] != '\'')
				P->pos++;
		} else if (c == '"') {
			P->pos++;
			while (P->pos < P->len && s[P->pos] != '"') {
				if (s[P->pos] == '\\')
					P->pos++;
				P->pos++;
			}
		} else if (c == '(')
			depth++;
		else if (c == ')')
			depth--;
		P->pos++;
	}
}

static void LexWord(Parser* P) {
	const char* s = P->src;
	int start = P->pos;
	bool quoted = false;
	while (P->pos < P->len) {
		char c = s[P->pos];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' || c == '&' || c == '|' ||
			c == '<' || c == '>' || c == '(' || c == ')')
			break;
		if (c == '\\') {
			quoted = true;
			if (P->pos + 1 < P->len && s[P->pos + 1] == '\n')
				P->line++;
			P->pos += 2;
			continue;
		}
		if (c == '\'') {
			quoted = true;
			P->pos++;
			while (P->pos < P->len && s[P->pos] != '\'') {
				if (s[P->pos] == '\n')
					P->line++;
				P->pos++;
			}
			P->pos++;
			continue;
		}
		if (c == '$' && P->pos + 1 < P->len && s[P->pos + 1] == '\'') { /* $'...' */
			quoted = true;
			P->pos += 2;
			while (P->pos < P->len && s[P->pos] != '\'') {
				if (s[P->pos] == '\\')
					P->pos++;
				P->pos++;
			}
			P->pos++;
			continue;
		}
		if (c == '"') {
			quoted = true;
			P->pos++;
			while (P->pos < P->len && s[P->pos] != '"') {
				if (s[P->pos] == '\\')
					P->pos++;
				else if (s[P->pos] == '$' && P->pos + 1 < P->len && s[P->pos + 1] == '(') {
					SkipParen(P);
					continue;
				} else if (s[P->pos] == '\n')
					P->line++;
				P->pos++;
			}
			P->pos++;
			continue;
		}
		if (c == '$' && P->pos + 1 < P->len && s[P->pos + 1] == '(') {
			SkipParen(P);
			continue;
		}
		if (c == '$' && P->pos + 1 < P->len && s[P->pos + 1] == '{') {
			int d = 0;
			while (P->pos < P->len) {
				if (s[P->pos] == '{')
					d++;
				else if (s[P->pos] == '}' && --d == 0) {
					P->pos++;
					break;
				}
				P->pos++;
			}
			continue;
		}
		if (c == '`') {
			P->pos++;
			while (P->pos < P->len && s[P->pos] != '`')
				P->pos++;
			P->pos++;
			continue;
		}
		P->pos++;
	}
	if (P->pos > P->len)
		P->pos = P->len;
	P->tok = T_WORD;
	P->quoted = quoted;
	P->text = ArenaStr(P->ar, s + start, P->pos - start);
}

static void Next(Parser* P) {
	const char* s = P->src;
	for (;;) {
		while (P->pos < P->len && IsSp(s[P->pos]))
			P->pos++;
		if (P->pos + 1 < P->len && s[P->pos] == '\\' && s[P->pos + 1] == '\n') {
			P->pos += 2;
			P->line++;
			continue;
		}
		if (P->pos < P->len && s[P->pos] == '#')
			while (P->pos < P->len && s[P->pos] != '\n')
				P->pos++;
		break;
	}
	P->tline = P->line;
	P->quoted = false;
	P->text = NULL;
	if (P->pos >= P->len) {
		P->tok = T_EOF;
		return;
	}
	char c = s[P->pos];
	int fd = -1;
	if (IsDig(c) && P->pos + 1 < P->len && (s[P->pos + 1] == '<' || s[P->pos + 1] == '>')) {
		fd = c - '0';
		P->pos++;
		c = s[P->pos];
	}
	char n1 = P->pos + 1 < P->len ? s[P->pos + 1] : 0;
	switch (c) {
	case '\n':
		P->pos++;
		P->line++;
		P->tok = T_NL;
		return;
	case ';':
		P->pos++;
		if (n1 == ';') {
			P->pos++;
			P->tok = T_DSEMI;
		} else
			P->tok = T_SEMI;
		return;
	case '&':
		P->pos++;
		if (n1 == '&') {
			P->pos++;
			P->tok = T_AND;
		} else
			P->tok = T_AMP;
		return;
	case '|':
		P->pos++;
		if (n1 == '|') {
			P->pos++;
			P->tok = T_OR;
		} else
			P->tok = T_PIPE;
		return;
	case '(':
		P->pos++;
		P->tok = T_LP;
		return;
	case ')':
		P->pos++;
		P->tok = T_RP;
		return;
	case '<':
		P->pos++;
		P->tok = T_REDIR;
		P->rop = R_IN;
		P->rfd = fd < 0 ? 0 : fd;
		return;
	case '>':
		P->pos++;
		P->tok = T_REDIR;
		P->rfd = fd < 0 ? 1 : fd;
		if (n1 == '>') {
			P->pos++;
			P->rop = R_APP;
		} else if (n1 == '&') {
			P->pos++;
			P->rop = R_DUP;
		} else
			P->rop = R_OUT;
		return;
	}
	LexWord(P);
}

static bool Kw(Parser* P, const char* k) {
	return P->tok == T_WORD && !P->quoted && strcmp(P->text, k) == 0;
}
static void SkipNl(Parser* P) {
	while (P->tok == T_NL)
		Next(P);
}
static void SkipSep(Parser* P) {
	while (P->tok == T_NL || P->tok == T_SEMI)
		Next(P);
}
static bool ExpectKw(Parser* P, const char* k) {
	if (!Kw(P, k)) {
		char m[64];
		snprintf(m, sizeof m, "syntax error: expected '%s'", k);
		PErr(P, m);
		return false;
	}
	Next(P);
	return true;
}
static bool AtEnd(Parser* P) {
	if (P->tok == T_EOF || P->tok == T_RP || P->tok == T_DSEMI)
		return true;
	if (P->tok == T_WORD && !P->quoted) {
		const char* t = P->text;
		switch (t[0]) {
		case 't':
			return !strcmp(t, "then");
		case 'e':
			return !strcmp(t, "elif") || !strcmp(t, "else") || !strcmp(t, "esac");
		case 'f':
			return !strcmp(t, "fi");
		case 'd':
			return !strcmp(t, "do") || !strcmp(t, "done");
		case '}':
			return t[1] == 0;
		}
	}
	return false;
}

static Node* ParseList(Parser* P);
static Node* ParseCommand(Parser* P);

static Node* ParseTrailRedirs(Parser* P, Node* n) {
	Redir** tail = &n->redir;
	while (*tail)
		tail = &(*tail)->next;
	while (P->tok == T_REDIR) {
		int fd = P->rfd, op = P->rop;
		Next(P);
		if (P->tok != T_WORD) {
			PErr(P, "syntax error: redirection needs a target");
			return n;
		}
		Redir* r = (Redir*)ArenaAlloc(P->ar, sizeof(Redir));
		memset(r, 0, sizeof(*r));
		r->fd = fd;
		r->op = op;
		r->target = P->text;
		Next(P);
		*tail = r;
		tail = &r->next;
	}
	return n;
}

static Node* ParseSimple(Parser* P) {
	Node* n = NewNode(P, N_CMD);
	int nw = 0;
	Redir** rt = &n->redir;
	while (P->tok == T_WORD || P->tok == T_REDIR) {
		if (P->tok == T_WORD) {
			if (nw >= 512) {
				PErr(P, "too many words in one command");
				return n;
			}
			P->scratch[nw++] = P->text;
			Next(P);
			if (nw == 1 && P->tok == T_LP) { /* name() { ... } */
				Node* f = NewNode(P, N_FUNC);
				f->s = P->scratch[0];
				Next(P);
				if (P->tok != T_RP) {
					PErr(P, "syntax error in function definition");
					return f;
				}
				Next(P);
				SkipNl(P);
				f->b = ParseCommand(P);
				return f;
			}
		} else {
			int fd = P->rfd, op = P->rop;
			Next(P);
			if (P->tok != T_WORD) {
				PErr(P, "syntax error: redirection needs a target");
				return n;
			}
			Redir* r = (Redir*)ArenaAlloc(P->ar, sizeof(Redir));
			memset(r, 0, sizeof(*r));
			r->fd = fd;
			r->op = op;
			r->target = P->text;
			Next(P);
			*rt = r;
			rt = &r->next;
		}
	}
	n->nw = nw;
	n->w = (char**)ArenaAlloc(P->ar, (size_t)(nw + 1) * sizeof(char*));
	memcpy(n->w, P->scratch, (size_t)nw * sizeof(char*));
	return n;
}

static Node* ParseIfBody(Parser* P) { /* 'if' / 'elif' already consumed */
	Node* n = NewNode(P, N_IF);
	n->a = ParseList(P);
	if (!ExpectKw(P, "then"))
		return n;
	n->b = ParseList(P);
	if (Kw(P, "elif")) {
		Next(P);
		n->c = ParseIfBody(P);
		return n;
	}
	if (Kw(P, "else")) {
		Next(P);
		n->c = ParseList(P);
	}
	ExpectKw(P, "fi");
	return n;
}

static Node* ParseCommand(Parser* P) {
	Node* n;
	if (P->tok == T_LP) {
		Next(P);
		n = NewNode(P, N_GROUP);
		n->b = ParseList(P);
		if (P->tok != T_RP) {
			PErr(P, "syntax error: expected ')'");
			return n;
		}
		Next(P);
		return ParseTrailRedirs(P, n);
	}
	if (P->tok == T_WORD && !P->quoted) {
		const char* t = P->text;
		if (!strcmp(t, "if")) {
			Next(P);
			n = ParseIfBody(P);
			return ParseTrailRedirs(P, n);
		}
		if (!strcmp(t, "while") || !strcmp(t, "until")) {
			n = NewNode(P, N_WHILE);
			n->flag = (uint8_t)(t[0] == 'u');
			Next(P);
			n->a = ParseList(P);
			if (!ExpectKw(P, "do"))
				return n;
			n->b = ParseList(P);
			ExpectKw(P, "done");
			return ParseTrailRedirs(P, n);
		}
		if (!strcmp(t, "for") || !strcmp(t, "select")) {
			n = NewNode(P, t[0] == 's' ? N_SELECT : N_FOR);
			Next(P);
			if (P->tok != T_WORD) {
				PErr(P, "syntax error: loop needs a variable name");
				return n;
			}
			n->s = P->text;
			Next(P);
			SkipNl(P);
			if (Kw(P, "in")) {
				Next(P);
				int nw = 0;
				while (P->tok == T_WORD) {
					if (nw >= 512) {
						PErr(P, "too many words in loop list");
						return n;
					}
					P->scratch[nw++] = P->text;
					Next(P);
				}
				n->nw = nw;
				n->w = (char**)ArenaAlloc(P->ar, (size_t)(nw + 1) * sizeof(char*));
				memcpy(n->w, P->scratch, (size_t)nw * sizeof(char*));
			} else
				n->flag = 1; /* no 'in': iterate "$@" */
			SkipSep(P);
			if (!ExpectKw(P, "do"))
				return n;
			n->b = ParseList(P);
			ExpectKw(P, "done");
			return ParseTrailRedirs(P, n);
		}
		if (!strcmp(t, "case")) {
			n = NewNode(P, N_CASE);
			Next(P);
			if (P->tok != T_WORD) {
				PErr(P, "syntax error: 'case' needs a word");
				return n;
			}
			n->s = P->text;
			Next(P);
			SkipNl(P);
			if (!ExpectKw(P, "in"))
				return n;
			CaseItem tmp[64];
			int ni = 0;
			for (;;) {
				SkipSep(P);
				if (P->tok == T_EOF) {
					PErr(P, "syntax error: missing 'esac'");
					return n;
				}
				if (Kw(P, "esac")) {
					Next(P);
					break;
				}
				if (ni >= 64) {
					PErr(P, "too many case items");
					return n;
				}
				if (P->tok == T_LP)
					Next(P);
				int np = 0;
				while (P->tok == T_WORD && np < 512) {
					P->scratch[np++] = P->text;
					Next(P);
					if (P->tok == T_PIPE)
						Next(P);
					else
						break;
				}
				if (np == 0 || P->tok != T_RP) {
					PErr(P, "syntax error in case pattern");
					return n;
				}
				Next(P);
				tmp[ni].np = np;
				tmp[ni].pats = (char**)ArenaAlloc(P->ar, (size_t)np * sizeof(char*));
				memcpy(tmp[ni].pats, P->scratch, (size_t)np * sizeof(char*));
				tmp[ni].body = ParseList(P);
				if (P->tok == T_DSEMI)
					Next(P);
				ni++;
			}
			n->n = ni;
			n->items = (CaseItem*)ArenaAlloc(P->ar, (size_t)(ni ? ni : 1) * sizeof(CaseItem));
			memcpy(n->items, tmp, (size_t)ni * sizeof(CaseItem));
			return ParseTrailRedirs(P, n);
		}
		if (!strcmp(t, "function")) {
			n = NewNode(P, N_FUNC);
			Next(P);
			if (P->tok != T_WORD) {
				PErr(P, "syntax error: 'function' needs a name");
				return n;
			}
			n->s = P->text;
			Next(P);
			if (P->tok == T_LP) {
				Next(P);
				if (P->tok != T_RP) {
					PErr(P, "syntax error in function definition");
					return n;
				}
				Next(P);
			}
			SkipNl(P);
			n->b = ParseCommand(P);
			return n;
		}
		if (t[0] == '{' && t[1] == 0) {
			n = NewNode(P, N_GROUP);
			Next(P);
			n->b = ParseList(P);
			if (!Kw(P, "}")) {
				PErr(P, "syntax error: expected '}'");
				return n;
			}
			Next(P);
			return ParseTrailRedirs(P, n);
		}
	}
	return ParseSimple(P);
}

static Node* ParsePipeline(Parser* P) {
	bool neg = false;
	if (Kw(P, "!")) {
		neg = true;
		Next(P);
	}
	Node* l = ParseCommand(P);
	while (P->tok == T_PIPE && !P->err) {
		Next(P);
		SkipNl(P);
		Node* r = ParseCommand(P);
		Node* p = NewNode(P, N_PIPE);
		p->a = l;
		p->b = r;
		l = p;
	}
	if (neg) {
		Node* nn = NewNode(P, N_NOT);
		nn->a = l;
		l = nn;
	}
	return l;
}

static Node* ParseAndOr(Parser* P) {
	Node* l = ParsePipeline(P);
	while ((P->tok == T_AND || P->tok == T_OR) && !P->err) {
		int type = P->tok == T_AND ? N_AND : N_OR;
		Next(P);
		SkipNl(P);
		Node* r = ParsePipeline(P);
		Node* n = NewNode(P, type);
		n->a = l;
		n->b = r;
		l = n;
	}
	return l;
}

static Node* ParseList(Parser* P) {
	Node *head = NULL, *tail = NULL;
	for (;;) {
		SkipSep(P);
		if (P->err || AtEnd(P))
			break;
		Node* n = ParseAndOr(P);
		if (!n || P->err)
			break;
		if (tail)
			tail->next = n;
		else
			head = n;
		tail = n;
		if (P->tok == T_AMP) /* no job control: '&' just sequences */
			Next(P);
		else if (P->tok != T_NL && P->tok != T_SEMI && !AtEnd(P)) {
			PErr(P, "syntax error: unexpected token");
			break;
		}
	}
	return head;
}

/* ------------------------------------------------------------------ */
/* bat parser (line oriented)                                          */
/* ------------------------------------------------------------------ */
static char* BatNextLine(Parser* P) {
	if (P->pos >= P->len)
		return NULL;
	int s = P->pos;
	while (P->pos < P->len && P->src[P->pos] != '\n')
		P->pos++;
	int e = P->pos;
	if (P->pos < P->len)
		P->pos++;
	P->line++;
	while (s < e && IsSp(P->src[s]))
		s++;
	while (e > s && IsSp(P->src[e - 1]))
		e--;
	return ArenaStr(P->ar, P->src + s, e - s);
}

/* next space-delimited token (quotes keep spaces together); NUL-terminates it */
static char* BatTok(char** ps) {
	char* s = TrimL(*ps);
	char* st = s;
	bool q = false;
	while (*s && (q || !IsSp(*s))) {
		if (*s == '"')
			q = !q;
		s++;
	}
	if (*s) {
		*s = 0;
		s++;
	}
	*ps = TrimL(s);
	return st;
}

static char* BatFindClose(char* s) { /* s is just after '(' */
	int depth = 1;
	bool q = false;
	for (; *s; s++) {
		if (*s == '"')
			q = !q;
		else if (!q && *s == '(')
			depth++;
		else if (!q && *s == ')' && --depth == 0)
			return s;
	}
	return NULL;
}

static Node* BatParseStmt(Parser* P, char* t);

static Node* BatParseBlock(Parser* P, char** after) {
	Node *head = NULL, *tail = NULL;
	for (;;) {
		char* ln = BatNextLine(P);
		if (!ln) {
			PErr(P, "missing ')' at end of script");
			*after = (char*)"";
			break;
		}
		if (*ln == ')') {
			*after = TrimL(ln + 1);
			break;
		}
		Node* n = BatParseStmt(P, ln);
		if (!n)
			continue;
		if (tail)
			tail->next = n;
		else
			head = n;
		tail = n;
	}
	return head;
}

static Node* BatParseBody(Parser* P, char* rest, char** after) {
	rest = TrimL(rest);
	*after = (char*)"";
	if (*rest == '(') {
		char* inner = TrimL(rest + 1);
		char* close = BatFindClose(rest + 1);
		if (close) {
			*close = 0;
			*after = TrimL(close + 1);
			return BatParseStmt(P, TrimL(rest + 1));
		}
		Node *head = NULL, *tail = NULL;
		if (*inner) {
			head = tail = BatParseStmt(P, inner);
		}
		Node* more = BatParseBlock(P, after);
		if (tail)
			tail->next = more;
		else
			head = more;
		return head;
	}
	return BatParseStmt(P, rest);
}

static int BatCmpCode(const char* op) {
	if (!strcmp(op, "==") || EqCI(op, "equ"))
		return CMP_EQ;
	if (EqCI(op, "neq"))
		return CMP_NE;
	if (EqCI(op, "lss"))
		return CMP_LT;
	if (EqCI(op, "leq"))
		return CMP_LE;
	if (EqCI(op, "gtr"))
		return CMP_GT;
	if (EqCI(op, "geq"))
		return CMP_GE;
	return 0;
}

static Node* BatParseIf(Parser* P, char* s) {
	Node* n = NewNode(P, N_BIF);
	n->flag = 0;
	s = TrimL(s);
	if (StartsKw(s, "/i")) {
		n->flag |= 2;
		s = TrimL(s + 2);
	}
	if (StartsKw(s, "not")) {
		n->flag |= 1;
		s = TrimL(s + 3);
	}
	char *o1 = NULL, *o2 = (char*)"";
	if (StartsKw(s, "exist")) {
		n->m = BK_EXIST;
		s = TrimL(s + 5);
		o1 = BatTok(&s);
	} else if (StartsKw(s, "defined")) {
		n->m = BK_DEF;
		s = TrimL(s + 7);
		o1 = BatTok(&s);
	} else if (StartsKw(s, "errorlevel")) {
		n->m = BK_ERR;
		s = TrimL(s + 10);
		o1 = BatTok(&s);
	} else {
		n->m = BK_CMP;
		char* t1 = BatTok(&s);
		char* eq = strstr(t1, "==");
		if (eq) {
			*eq = 0;
			o1 = t1;
			o2 = eq + 2;
			n->n = CMP_EQ;
		} else {
			char* op = BatTok(&s);
			o1 = t1;
			o2 = BatTok(&s);
			n->n = BatCmpCode(op);
			if (!n->n) {
				PErr(P, "bad comparison operator in 'if'");
				return n;
			}
		}
	}
	n->w = (char**)ArenaAlloc(P->ar, 2 * sizeof(char*));
	n->w[0] = o1;
	n->w[1] = o2;
	n->nw = 2;
	char* after = (char*)"";
	n->b = BatParseBody(P, s, &after);
	if (StartsKw(after, "else")) {
		char* after2;
		n->c = BatParseBody(P, after + 4, &after2);
	}
	return n;
}

static Node* BatParseFor(Parser* P, char* s) {
	Node* n = NewNode(P, N_BFOR);
	s = TrimL(s);
	if (s[0] == '/' && Lc(s[1]) == 'l') {
		n->flag = 1;
		s = TrimL(s + 2);
	}
	if (s[0] != '%') {
		PErr(P, "bad 'for' syntax");
		return n;
	}
	while (*s == '%')
		s++;
	char vn[3] = {'%', *s, 0};
	n->s = ArenaStr(P->ar, vn, 2);
	s = TrimL(s + 1);
	if (!StartsKw(s, "in")) {
		PErr(P, "bad 'for' syntax: expected 'in'");
		return n;
	}
	s = TrimL(s + 2);
	if (*s != '(') {
		PErr(P, "bad 'for' syntax: expected '('");
		return n;
	}
	char* close = BatFindClose(s + 1);
	if (!close) {
		PErr(P, "bad 'for' syntax: missing ')'");
		return n;
	}
	*close = 0;
	n->w = (char**)ArenaAlloc(P->ar, sizeof(char*));
	n->w[0] = s + 1;
	n->nw = 1;
	s = TrimL(close + 1);
	if (!StartsKw(s, "do")) {
		PErr(P, "bad 'for' syntax: expected 'do'");
		return n;
	}
	char* after;
	n->b = BatParseBody(P, s + 2, &after);
	return n;
}

static Node* BatParseStmt(Parser* P, char* t) {
	bool silent = false;
	t = TrimL(t);
	while (*t == '@') {
		silent = true;
		t = TrimL(t + 1);
	}
	if (!*t || (t[0] == ':' && t[1] == ':'))
		return NULL;
	if (StartsKw(t, "rem"))
		return NULL;
	if (StartsKw(t, "if"))
		return BatParseIf(P, t + 2);
	if (StartsKw(t, "for"))
		return BatParseFor(P, t + 3);
	if (StartsKw(t, "goto")) {
		Node* n = NewNode(P, N_GOTO);
		char* l = TrimL(t + 4);
		if (*l == ':')
			l++;
		int len = (int)strlen(l);
		n->s = ArenaStr(P->ar, l, len);
		for (char* c = n->s; *c; c++)
			*c = Uc(*c);
		return n;
	}
	if (*t == '(') {
		Node* n = NewNode(P, N_GROUP);
		char* after;
		n->b = BatParseBody(P, t, &after);
		return n;
	}
	/* split at the LAST unquoted && or || -> left-associative chain */
	{
		bool q = false;
		int last = -1;
		for (int i = 0; t[i]; i++) {
			if (t[i] == '"')
				q = !q;
			else if (!q && ((t[i] == '&' && t[i + 1] == '&') || (t[i] == '|' && t[i + 1] == '|'))) {
				last = i;
				i++;
			}
		}
		if (last >= 0) {
			int type = t[last] == '&' ? N_AND : N_OR;
			Node* n = NewNode(P, type);
			char* right = TrimL(t + last + 2);
			t[last] = 0;
			n->a = BatParseStmt(P, t);
			n->b = BatParseStmt(P, right);
			if (!n->a || !n->b)
				return n->a ? n->a : n->b;
			return n;
		}
	}
	Node* n = NewNode(P, N_BCMD);
	n->s = t;
	n->flag = silent;
	return n;
}

static bool ParseBat(Parser* P, Prog* pr) {
	Node *head = NULL, *tail = NULL;
	int cnt = 0;
	char* ln;
	while ((ln = BatNextLine(P)) != NULL && !P->err) {
		if (ln[0] == ':' && ln[1] != ':') {
			if (P->nlabs < 256) {
				char* nm = ln + 1;
				char* e = nm;
				while (*e && !IsSp(*e))
					e++;
				*e = 0;
				for (char* c = nm; *c; c++)
					*c = Uc(*c);
				P->labs[P->nlabs].name = nm;
				P->labs[P->nlabs].idx = cnt;
				P->nlabs++;
			}
			continue;
		}
		Node* n = BatParseStmt(P, ln);
		if (!n)
			continue;
		if (tail)
			tail->next = n;
		else
			head = n;
		tail = n;
		cnt++;
	}
	if (P->err)
		return false;
	pr->nst = cnt;
	pr->st = (Node**)ArenaAlloc(P->ar, (size_t)(cnt ? cnt : 1) * sizeof(Node*));
	int i = 0;
	for (Node* n = head; n;) {
		Node* nx = n->next;
		n->next = NULL; /* top-level statements are indexed, not chained */
		pr->st[i++] = n;
		n = nx;
	}
	int sz = 8;
	while (sz < P->nlabs * 2)
		sz <<= 1;
	pr->tab = (Label*)ArenaAlloc(P->ar, (size_t)sz * sizeof(Label));
	memset(pr->tab, 0, (size_t)sz * sizeof(Label));
	pr->tabMask = sz - 1;
	for (int k = 0; k < P->nlabs; k++) {
		int h = (int)(Fnv(P->labs[k].name) & (uint32_t)pr->tabMask);
		while (pr->tab[h].name) {
			if (!strcmp(pr->tab[h].name, P->labs[k].name))
				break; /* first definition wins */
			h = (h + 1) & pr->tabMask;
		}
		if (!pr->tab[h].name)
			pr->tab[h] = P->labs[k];
	}
	return true;
}

static int ProgLabel(Prog* pr, const char* upper) {
	if (!pr || !pr->tab)
		return -1;
	int h = (int)(Fnv(upper) & (uint32_t)pr->tabMask);
	while (pr->tab[h].name) {
		if (!strcmp(pr->tab[h].name, upper))
			return pr->tab[h].idx;
		h = (h + 1) & pr->tabMask;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* expansion                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
	char** v;
	int n, cap;
} ArgVec;
static bool AvPush(ArgVec* a, char* s) {
	if (a->n == a->cap) {
		int nc = a->cap ? a->cap * 2 : 16;
		char** nv = (char**)realloc(a->v, (size_t)nc * sizeof(char*));
		if (!nv)
			return false;
		a->v = nv;
		a->cap = nc;
	}
	a->v[a->n++] = s;
	return true;
}
static void AvFree(ArgVec* a) {
	free(a->v);
	a->v = NULL;
	a->n = a->cap = 0;
}

/* pat: pattern mode (case). Characters that came out of quotes get a '\'
 * in front of any glob metacharacter so GlobMatch treats them literally. */
typedef struct {
	char buf[XS_FIELD_MAX];
	int bl;
	bool have, split, fail, pat, inq;
	ArgVec* av;
} Ex;

static void ExInit(Ex* e, bool split, bool pat, ArgVec* av) {
	e->bl = 0;
	e->have = false;
	e->split = split;
	e->fail = false;
	e->pat = pat;
	e->inq = false;
	e->av = av;
}

static void ExPut(Ex* e, char c) {
	if (e->pat && e->inq && (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')) {
		if (e->bl < XS_FIELD_MAX - 1)
			e->buf[e->bl++] = '\\';
	}
	if (e->bl < XS_FIELD_MAX - 1)
		e->buf[e->bl++] = c;
	e->have = true;
}
static void ExEnd(Ex* e) {
	if (!e->have)
		return;
	if (e->av) {
		char* s = (char*)ScratchAlloc((size_t)e->bl + 1);
		if (!s)
			e->fail = true;
		else {
			memcpy(s, e->buf, (size_t)e->bl);
			s[e->bl] = 0;
			if (!AvPush(e->av, s))
				e->fail = true;
		}
	}
	e->bl = 0;
	e->have = false;
}
static void ExPutStr(Ex* e, const char* s, bool quoted) {
	for (; *s; s++) {
		if (!quoted && e->split && (*s == ' ' || *s == '\t' || *s == '\n'))
			ExEnd(e);
		else
			ExPut(e, *s);
	}
}

static int ExecString(const char* text, int len, bool propagate);
static void ExScan(Ex* e, const char* w);
static void ExpandString(const char* w, char* out, int max);

/* run `cmd` with stdout captured; returns malloc'd text, trailing newlines stripped */
static char* CmdSubst(const char* cmd, int len) {
	if (g_substdepth >= 6) {
		printf("[xesh]: command substitution nested too deep\r\n");
		return NULL;
	}
	int id = g_capdepth++;
	char name[] = "/xscapN.tmp";
	name[6] = (char)('0' + id);
	int w = XsHostOpen(name, 1);
	if (w < 0) {
		g_capdepth--;
		printf("[xesh]: cannot create capture file %s\r\n", name);
		return NULL;
	}
	int so = g_fd_out;
	g_fd_out = w;
	g_substdepth++;
	int st = ExecString(cmd, len, false);
	g_substdepth--;
	g_fd_out = so;
	XsHostClose(w);
	RbInvalidate(w);
	g_capdepth--;
	g_status = st;

	size_t cap = 1024, n = 0;
	char* out = (char*)malloc(cap);
	if (!out)
		return NULL;
	int r = XsHostOpen(name, 0);
	if (r >= 0) {
		for (;;) {
			if (n + 512 >= cap) {
				cap *= 2;
				char* nb = (char*)realloc(out, cap);
				if (!nb) {
					free(out);
					XsHostClose(r);
					return NULL;
				}
				out = nb;
			}
			int got = XsHostRead(r, out + n, (int)(cap - n - 1));
			if (got <= 0)
				break;
			n += (size_t)got;
		}
		XsHostClose(r);
		RbInvalidate(r);
	}
	XsHostRemove(name);
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		n--;
	out[n] = 0;
	return out;
}

static void ExSubst(Ex* e, const char* cmd, int len, bool q) {
	char* out = CmdSubst(cmd, len);
	if (!out)
		return;
	ExPutStr(e, out, q);
	free(out);
}

static const char* ExBacktick(Ex* e, const char* p, bool q) {
	const char* s = p + 1;
	const char* t = s;
	while (*t && *t != '`')
		t++;
	ExSubst(e, s, (int)(t - s), q);
	return *t ? t + 1 : t;
}

/* ${name...}: ${x} ${x:-w} ${x-w} ${x:=w} ${x=w} ${x:+w} ${x+w} ${#x} ${x%p} ${x%%p} ${x#p} ${x##p} */
static void ExBrace(Ex* e, const char* in, int len, bool q) {
	char inner[XS_FIELD_MAX];
	if (len >= (int)sizeof inner)
		len = (int)sizeof inner - 1;
	memcpy(inner, (void*)in, (size_t)len);
	inner[len] = 0;

	char name[XS_NAME_MAX];
	int ni = 0;
	const char* p = inner;
	bool lenOp = false;
	if (*p == '#' && p[1]) {
		lenOp = true;
		p++;
	}
	if (IsDig(*p)) {
		while (IsDig(*p) && ni < XS_NAME_MAX - 1)
			name[ni++] = *p++;
	} else if (*p == '?' || *p == '#' || *p == '@' || *p == '*' || *p == '$') {
		name[ni++] = *p++;
	} else {
		while (IsIdC(*p) && ni < XS_NAME_MAX - 1)
			name[ni++] = *p++;
	}
	name[ni] = 0;

	const char* val = NULL;
	char tmp[24];
	if (name[0] == '?') {
		FmtI64(g_status, tmp);
		val = tmp;
	} else if (name[0] == '#') {
		FmtI64(PosCount(), tmp);
		val = tmp;
	} else if (name[0] == '$') {
		val = "1";
	} else if (IsDig(name[0]))
		val = PosArg(atoi(name));
	else
		val = VarGet(name);

	if (lenOp) {
		FmtI64(val ? (int64_t)strlen(val) : 0, tmp);
		ExPutStr(e, tmp, true);
		return;
	}
	if (*p == 0) {
		if (val)
			ExPutStr(e, val, q);
		return;
	}
	bool colon = false;
	if (*p == ':') {
		colon = true;
		p++;
	}
	char op = *p;
	if (op == '-' || op == '=' || op == '+') {
		const char* word = p + 1;
		bool unset = !val || (colon && !*val);
		char exp[XS_FIELD_MAX];
		if (op == '+') {
			if (!unset) {
				ExpandString(word, exp, (int)sizeof exp);
				ExPutStr(e, exp, q);
			}
			return;
		}
		if (!unset) {
			ExPutStr(e, val, q);
			return;
		}
		ExpandString(word, exp, (int)sizeof exp);
		if (op == '=' && ni > 0 && !IsDig(name[0]))
			VarSet(name, exp);
		ExPutStr(e, exp, q);
		return;
	}
	if ((op == '%' || op == '#') && val) {
		bool longest = p[1] == op;
		const char* pat = p + (longest ? 2 : 1);
		int vl = (int)strlen(val);
		char cut[XS_VAL_MAX];
		strncpy(cut, val, XS_VAL_MAX - 1);
		cut[XS_VAL_MAX - 1] = 0;
		if (op == '%') {
			int found = vl;
			if (longest) {
				for (int i = 0; i <= vl; i++)
					if (GlobMatch(pat, val + i)) {
						found = i;
						break;
					}
			} else {
				for (int i = vl; i >= 0; i--)
					if (GlobMatch(pat, val + i)) {
						found = i;
						break;
					}
			}
			if (found < XS_VAL_MAX)
				cut[found] = 0;
			ExPutStr(e, cut, q);
		} else {
			int found = 0;
			char sub[XS_VAL_MAX];
			if (longest) {
				for (int i = vl; i >= 0; i--) {
					if (i >= XS_VAL_MAX)
						continue;
					memcpy(sub, (void*)val, (size_t)i);
					sub[i] = 0;
					if (GlobMatch(pat, sub)) {
						found = i;
						break;
					}
				}
			} else {
				for (int i = 0; i <= vl && i < XS_VAL_MAX; i++) {
					memcpy(sub, (void*)val, (size_t)i);
					sub[i] = 0;
					if (GlobMatch(pat, sub)) {
						found = i;
						break;
					}
				}
			}
			ExPutStr(e, val + found, q);
		}
		return;
	}
	if (val)
		ExPutStr(e, val, q);
}

static const char* ExDollar(Ex* e, const char* p, bool q) {
	char c = p[1];
	if (c == '(') {
		if (p[2] == '(') { /* $(( arithmetic )) */
			const char* s = p + 3;
			const char* t = s;
			int depth = 0;
			while (*t) {
				if (*t == '(')
					depth++;
				else if (*t == ')') {
					if (depth == 0 && t[1] == ')')
						break;
					depth--;
				}
				t++;
			}
			if (*t) {
				char ex[256];
				int l = (int)(t - s);
				if (l > 255)
					l = 255;
				memcpy(ex, (void*)s, (size_t)l);
				ex[l] = 0;
				char expanded[256];
				ExpandString(ex, expanded, (int)sizeof expanded);
				bool err;
				int64_t v = ArithEval(expanded, &err);
				if (err)
					printf("[xesh]: arithmetic error in $((%s))\r\n", expanded);
				char num[24];
				FmtI64(v, num);
				ExPutStr(e, num, true);
				return t + 2;
			}
		}
		const char* s = p + 2;
		const char* t = s;
		int depth = 1;
		while (*t) {
			char ch = *t;
			if (ch == '\'') {
				t++;
				while (*t && *t != '\'')
					t++;
			} else if (ch == '"') {
				t++;
				while (*t && *t != '"') {
					if (*t == '\\' && t[1])
						t++;
					t++;
				}
			} else if (ch == '(')
				depth++;
			else if (ch == ')' && --depth == 0)
				break;
			if (*t)
				t++;
		}
		ExSubst(e, s, (int)(t - s), q);
		return *t ? t + 1 : t;
	}
	if (c == '{') {
		const char* s = p + 2;
		const char* t = s;
		int depth = 1;
		while (*t) {
			if (*t == '{')
				depth++;
			else if (*t == '}' && --depth == 0)
				break;
			t++;
		}
		ExBrace(e, s, (int)(t - s), q);
		return *t ? t + 1 : t;
	}
	if (c == '?') {
		char b[24];
		FmtI64(g_status, b);
		ExPutStr(e, b, true);
		return p + 2;
	}
	if (c == '#') {
		char b[24];
		FmtI64(PosCount(), b);
		ExPutStr(e, b, true);
		return p + 2;
	}
	if (c == '$') {
		ExPutStr(e, "1", true);
		return p + 2;
	}
	if (c == '@' || c == '*') {
		int n = PosCount();
		bool separate = (c == '@') && q && e->split;
		for (int i = 1; i <= n; i++) {
			const char* a = PosArg(i);
			if (separate) {
				e->have = true;
				ExPutStr(e, a, true);
				if (i < n)
					ExEnd(e);
			} else {
				ExPutStr(e, a, q);
				if (i < n)
					ExPutStr(e, " ", q);
			}
		}
		return p + 2;
	}
	if (IsDig(c)) {
		const char* a = PosArg(c - '0');
		if (a)
			ExPutStr(e, a, q);
		return p + 2;
	}
	if (IsIdS(c)) {
		char name[XS_NAME_MAX];
		int n = 0;
		const char* t = p + 1;
		while (IsIdC(*t)) {
			if (n < XS_NAME_MAX - 1)
				name[n++] = *t;
			t++;
		}
		name[n] = 0;
		const char* v = VarGet(name);
		if (v)
			ExPutStr(e, v, q);
		return t;
	}
	ExPut(e, '$');
	return p + 1;
}

static void ExScan(Ex* e, const char* w) {
	const char* p = w;
	while (*p && !e->fail) {
		char c = *p;
		if (c == '\\') {
			p++;
			if (*p == '\n') {
				p++;
				continue;
			}
			if (*p) {
				bool sq = e->inq;
				e->inq = true;
				ExPut(e, *p);
				e->inq = sq;
				p++;
			}
			continue;
		}
		if (c == '\'') {
			p++;
			e->have = true;
			e->inq = true;
			while (*p && *p != '\'')
				ExPut(e, *p++);
			e->inq = false;
			if (*p)
				p++;
			continue;
		}
		if (c == '$' && p[1] == '\'') { /* ANSI-C quoting: $'\033[1m' */
			p += 2;
			e->have = true;
			e->inq = true;
			while (*p && *p != '\'') {
				if (*p == '\\' && p[1]) {
					p++;
					int ch = EscSeq(&p);
					if (ch)
						ExPut(e, (char)ch);
					p++;
				} else
					ExPut(e, *p++);
			}
			e->inq = false;
			if (*p)
				p++;
			continue;
		}
		if (c == '"') {
			if (p[1] == '$' && p[2] == '@' && p[3] == '"') {
				int n = PosCount();
				e->inq = true;
				for (int i = 1; i <= n; i++) {
					const char* a = PosArg(i);
					if (e->split) {
						e->have = true;
						ExPutStr(e, a, true);
						if (i < n)
							ExEnd(e);
					} else {
						ExPutStr(e, a, true);
						if (i < n)
							ExPut(e, ' ');
					}
				}
				e->inq = false;
				p += 4;
				continue;
			}
			p++;
			e->have = true;
			e->inq = true;
			while (*p && *p != '"') {
				if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$' || p[1] == '`')) {
					ExPut(e, p[1]);
					p += 2;
				} else if (*p == '$')
					p = ExDollar(e, p, true);
				else if (*p == '`')
					p = ExBacktick(e, p, true);
				else
					ExPut(e, *p++);
			}
			e->inq = false;
			if (*p)
				p++;
			continue;
		}
		if (c == '$') {
			p = ExDollar(e, p, false);
			continue;
		}
		if (c == '`') {
			p = ExBacktick(e, p, false);
			continue;
		}
		if (c == '~' && p == w && (p[1] == 0 || p[1] == '/')) {
			if (!p[1])
				ExPut(e, '/');
			p++;
			continue;
		}
		ExPut(e, c);
		p++;
	}
}

static bool ExpandWord(const char* w, ArgVec* av) {
	Ex ex;
	ExInit(&ex, true, false, av);
	ExScan(&ex, w);
	ExEnd(&ex);
	return !ex.fail;
}

static void ExpandString(const char* w, char* out, int max) {
	Ex ex;
	ExInit(&ex, false, false, NULL);
	ExScan(&ex, w);
	int n = ex.bl < max - 1 ? ex.bl : max - 1;
	memcpy(out, ex.buf, (size_t)n);
	out[n] = 0;
}

static void ExpandPattern(const char* w, char* out, int max) {
	Ex ex;
	ExInit(&ex, false, true, NULL);
	ExScan(&ex, w);
	int n = ex.bl < max - 1 ? ex.bl : max - 1;
	memcpy(out, ex.buf, (size_t)n);
	out[n] = 0;
}

/* bat: %VAR%  %1  %*  %~1  %%i  %ERRORLEVEL%  %CD% */
static void BatExpand(const char* in, char* out, int max) {
	int o = 0;
#define BPUT(s)                                                                                    \
	do {                                                                                           \
		const char* _s = (s);                                                                      \
		while (*_s && o < max - 1)                                                                 \
			out[o++] = *_s++;                                                                      \
	} while (0)
	while (*in && o < max - 1) {
		if (*in != '%') {
			out[o++] = *in++;
			continue;
		}
		if (in[1] == '%') {
			if (IsAlpha(in[2])) {
				char nm[3] = {'%', in[2], 0};
				const char* v = VarGet(nm);
				if (v) {
					BPUT(v);
					in += 3;
					continue;
				}
			}
			out[o++] = '%';
			in += 2;
			continue;
		}
		if (in[1] == '*') {
			int n = PosCount();
			for (int i = 1; i <= n; i++) {
				BPUT(PosArg(i));
				if (i < n)
					BPUT(" ");
			}
			in += 2;
			continue;
		}
		if (in[1] == '~' && IsDig(in[2])) {
			const char* a = PosArg(in[2] - '0');
			if (a) {
				int l = (int)strlen(a);
				if (l >= 2 && a[0] == '"' && a[l - 1] == '"') {
					for (int i = 1; i < l - 1 && o < max - 1; i++)
						out[o++] = a[i];
				} else
					BPUT(a);
			}
			in += 3;
			continue;
		}
		if (IsDig(in[1])) {
			const char* a = PosArg(in[1] - '0');
			if (a)
				BPUT(a);
			in += 2;
			continue;
		}
		const char* e = strchr(in + 1, '%');
		if (!e) {
			out[o++] = *in++;
			continue;
		}
		char nm[XS_NAME_MAX];
		int nl = (int)(e - in - 1);
		if (nl <= 0 || nl >= XS_NAME_MAX) {
			out[o++] = *in++;
			continue;
		}
		memcpy(nm, (void*)(in + 1), (size_t)nl);
		nm[nl] = 0;
		if (EqCI(nm, "errorlevel")) {
			char b[24];
			FmtI64(g_status, b);
			BPUT(b);
		} else if (EqCI(nm, "cd")) {
			BPUT(XsHostCwd());
		} else {
			const char* v = VarGet(nm);
			if (v)
				BPUT(v);
		}
		in = e + 1;
	}
	out[o] = 0;
#undef BPUT
}

/* ------------------------------------------------------------------ */
/* execution                                                           */
/* ------------------------------------------------------------------ */
static int ExecNode(Node* n);
static int ExecList(Node* n);
static int RunArgs(int argc, char** argv, bool hostOnly);

typedef struct {
	int in, out, err;
	int opened[8];
	int no;
} IoSave;

static void RestoreIo(IoSave* sv) {
	for (int i = 0; i < sv->no; i++) {
		XsHostClose(sv->opened[i]);
		RbInvalidate(sv->opened[i]);
	}
	g_fd_in = sv->in;
	g_fd_out = sv->out;
	g_fd_err = sv->err;
	sv->no = 0;
}

static int ApplyRedirs(Redir* r, IoSave* sv) {
	sv->in = g_fd_in;
	sv->out = g_fd_out;
	sv->err = g_fd_err;
	sv->no = 0;
	for (; r; r = r->next) {
		char path[XS_FIELD_MAX];
		if (r->raw) {
			strncpy(path, r->target, sizeof path - 1);
			path[sizeof path - 1] = 0;
		} else
			ExpandString(r->target, path, (int)sizeof path);
		if (r->op == R_DUP) {
			int tf = path[0] == '2' ? g_fd_err : (path[0] == '1' ? g_fd_out : g_fd_in);
			if (r->fd == 2)
				g_fd_err = tf;
			else
				g_fd_out = tf;
			continue;
		}
		int fd;
		if (!strcmp(path, "/dev/null") || (r->raw && EqCI(path, "nul")))
			fd = XS_FD_NULL;
		else {
			fd = XsHostOpen(path, r->op == R_IN ? 0 : (r->op == R_APP ? 2 : 1));
			if (fd < 0) {
				printf("[xesh]: cannot open %s\r\n", path);
				RestoreIo(sv);
				return -1;
			}
			if (sv->no < 8)
				sv->opened[sv->no++] = fd;
		}
		if (r->op == R_IN)
			g_fd_in = fd;
		else if (r->fd == 2)
			g_fd_err = fd;
		else
			g_fd_out = fd;
	}
	return 0;
}

static int NullSinkFd(void) {
	return XsHostOpen("/xsnull.tmp", 1);
}

typedef int (*BuiltinFn)(int, char**);
typedef struct {
	const char* name;
	BuiltinFn fn;
} BuiltinDef;
static const BuiltinDef* BuiltinFind(const char* name);

static int CallFunc(Func* f, int argc, char** argv) {
	if (g_fp + 1 >= XS_MAX_FRAMES) {
		printf("[xesh]: function calls nested too deep\r\n");
		g_flow = FL_EXIT;
		return 1;
	}
	Frame* fr = &g_frames[++g_fp];
	fr->argc = argc;
	fr->argv = argv;
	fr->shift = 0;
	fr->own = NULL;
	fr->nown = 0;
	int sl = g_loopdepth;
	g_loopdepth = 0;
	ExecList(f->body);
	g_loopdepth = sl;
	if (g_flow == FL_RETURN)
		g_flow = FL_NONE;
	FrameFreeOwn(fr);
	g_fp--;
	return g_status;
}

static int RunArgs(int argc, char** argv, bool hostOnly) {
	if (argc == 0)
		return 0;
	if (!hostOnly) {
		Func* f = FuncFind(argv[0]);
		if (f)
			return CallFunc(f, argc, argv);
		const BuiltinDef* b = BuiltinFind(argv[0]);
		if (b)
			return b->fn(argc, argv);
	}
	int fo = g_fd_out, fe = g_fd_err, fi = g_fd_in;
	int tmpOut = -1, tmpErr = -1, tmpIn = -1;
	if (fo == XS_FD_NULL)
		fo = tmpOut = NullSinkFd();
	if (fe == XS_FD_NULL)
		fe = tmpErr = NullSinkFd();
	if (fi == XS_FD_NULL)
		fi = tmpIn = NullSinkFd();
	int st = XsHostRun(argc, argv, fi, fo, fe);
	if (tmpOut >= 0)
		XsHostClose(tmpOut);
	if (tmpErr >= 0)
		XsHostClose(tmpErr);
	if (tmpIn >= 0)
		XsHostClose(tmpIn);
	return st;
}

static int ExecSimple(Node* n) {
	size_t mark = g_stop;
	ArgVec av = {NULL, 0, 0};
	int i = 0;
	for (; i < n->nw; i++) { /* leading NAME=value assignments */
		const char* w = n->w[i];
		int k = 0;
		if (!IsIdS(w[0]))
			break;
		while (IsIdC(w[k]))
			k++;
		if (w[k] != '=' || k >= XS_NAME_MAX)
			break;
		char name[XS_NAME_MAX];
		memcpy(name, (void*)w, (size_t)k);
		name[k] = 0;
		char val[XS_VAL_MAX * 2];
		ExpandString(w + k + 1, val, (int)sizeof val);
		VarSet(name, val);
	}
	bool ok = true;
	for (; i < n->nw && ok; i++)
		ok = ExpandWord(n->w[i], &av);
	if (!ok) {
		printf("[xesh]: line %d: expansion too large\r\n", n->line);
		g_status = 1;
	} else if (av.n == 0) {
		g_status = 0;
	} else {
		g_status = RunArgs(av.n, av.v, false);
	}
	AvFree(&av);
	g_stop = mark;
	return g_status;
}

/* loop-flow bookkeeping shared by while/for/select/bfor. true = leave the loop */
static bool LoopFlow(void) {
	if (g_flow == FL_BREAK) {
		if (--g_flow_n <= 0)
			g_flow = FL_NONE;
		return true;
	}
	if (g_flow == FL_CONT) {
		if (--g_flow_n <= 0) {
			g_flow = FL_NONE;
			return false;
		}
		return true;
	}
	return g_flow != FL_NONE;
}

static int ExecWhile(Node* n) {
	int st = 0;
	g_loopdepth++;
	for (;;) {
		ExecList(n->a);
		if (g_flow)
			break;
		bool ok = (g_status == 0);
		if (n->flag)
			ok = !ok;
		if (!ok)
			break;
		st = ExecList(n->b);
		if (g_flow && LoopFlow())
			break;
		if (g_interrupted) {
			g_flow = FL_EXIT;
			break;
		}
	}
	g_loopdepth--;
	if (!g_flow)
		g_status = st;
	return g_status;
}

static bool BuildLoopList(Node* n, ArgVec* av) {
	bool ok = true;
	if (n->flag) {
		int cnt = PosCount();
		for (int i = 1; i <= cnt && ok; i++)
			ok = AvPush(av, (char*)PosArg(i));
	} else {
		for (int i = 0; i < n->nw && ok; i++)
			ok = ExpandWord(n->w[i], av);
	}
	return ok;
}

static int ExecFor(Node* n) {
	size_t mark = g_stop;
	ArgVec av = {NULL, 0, 0};
	if (!BuildLoopList(n, &av)) {
		printf("[xesh]: line %d: expansion too large\r\n", n->line);
		AvFree(&av);
		g_stop = mark;
		g_status = 1;
		return 1;
	}
	int st = 0;
	g_loopdepth++;
	for (int i = 0; i < av.n; i++) {
		VarSet(n->s, av.v[i]);
		st = ExecList(n->b);
		if (g_flow && LoopFlow())
			break;
		if (g_interrupted) {
			g_flow = FL_EXIT;
			break;
		}
	}
	g_loopdepth--;
	AvFree(&av);
	g_stop = mark;
	if (!g_flow)
		g_status = st;
	return g_status;
}

/* bash-style `select`: numbered text menu, reads a line per round */
static int ExecSelect(Node* n) {
	size_t mark = g_stop;
	ArgVec av = {NULL, 0, 0};
	if (!BuildLoopList(n, &av)) {
		AvFree(&av);
		g_stop = mark;
		g_status = 1;
		return 1;
	}
	const char* ps3 = VarGet("PS3");
	if (!ps3)
		ps3 = "#? ";
	int st = 0;
	bool redraw = true;
	g_loopdepth++;
	for (;;) {
		if (redraw) {
			for (int i = 0; i < av.n; i++) {
				char num[24];
				FmtI64(i + 1, num);
				XsPuts(-1, num);
				XsPuts(-1, ") ");
				XsPuts(-1, av.v[i]);
				XsPuts(-1, "\r\n");
			}
			redraw = false;
		}
		XsPuts(-1, ps3);
		char line[256];
		if (!ReadLineIn(line, (int)sizeof line))
			break;
		VarSet("REPLY", line);
		if (!line[0]) {
			redraw = true;
			continue;
		}
		bool okn;
		int64_t v = ParseI64(line, &okn);
		VarSet(n->s, (okn && v >= 1 && v <= av.n) ? av.v[v - 1] : "");
		st = ExecList(n->b);
		if (g_flow && LoopFlow())
			break;
		if (g_interrupted) {
			g_flow = FL_EXIT;
			break;
		}
	}
	g_loopdepth--;
	AvFree(&av);
	g_stop = mark;
	if (!g_flow)
		g_status = st;
	return g_status;
}

static int ExecCase(Node* n) {
	size_t mark = g_stop;
	char word[XS_FIELD_MAX];
	ExpandString(n->s, word, (int)sizeof word);
	for (int i = 0; i < n->n; i++) {
		bool hit = false;
		for (int k = 0; k < n->items[i].np && !hit; k++) {
			char pat[XS_FIELD_MAX];
			ExpandPattern(n->items[i].pats[k], pat, (int)sizeof pat);
			hit = GlobMatch(pat, word);
		}
		if (hit) {
			g_stop = mark;
			return ExecList(n->items[i].body);
		}
	}
	g_stop = mark;
	g_status = 0;
	return 0;
}

/* a | b : left writes a temp file, right reads it. Linear in the data. */
static int ExecPipe(Node* n) {
	if (g_pipedepth >= 8) {
		printf("[xesh]: pipeline nested too deep\r\n");
		g_status = 1;
		return 1;
	}
	int id = g_pipedepth++;
	char name[] = "/xspipeN.tmp";
	name[7] = (char)('0' + id);
	int w = XsHostOpen(name, 1);
	if (w < 0) {
		g_pipedepth--;
		printf("[xesh]: cannot create pipe file %s\r\n", name);
		g_status = 1;
		return 1;
	}
	int so = g_fd_out;
	g_fd_out = w;
	ExecNode(n->a);
	g_fd_out = so;
	XsHostClose(w);
	RbInvalidate(w);
	if (!g_flow) {
		int r = XsHostOpen(name, 0);
		if (r >= 0) {
			int si = g_fd_in;
			g_fd_in = r;
			ExecNode(n->b);
			g_fd_in = si;
			XsHostClose(r);
			RbInvalidate(r);
		}
	}
	XsHostRemove(name);
	g_pipedepth--;
	return g_status;
}

/* ---- bat nodes ---- */
static int BatSplit(char* s, char** argv, int max) {
	int n = 0;
	while (*s && n < max - 1) {
		s = TrimL(s);
		if (!*s)
			break;
		char* out = s;
		argv[n++] = out;
		bool q = false;
		while (*s && (q || !IsSp(*s))) {
			if (*s == '"')
				q = !q;
			else
				*out++ = *s;
			s++;
		}
		if (*s)
			s++;
		*out = 0;
	}
	argv[n] = NULL;
	return n;
}

static int ExecBIf(Node* n) {
	char a[512], b[512];
	bool r = false;
	BatExpand(n->w[0], a, (int)sizeof a);
	switch (n->m) {
	case BK_EXIST: {
		char* p = a;
		int l = (int)strlen(p);
		if (l >= 2 && p[0] == '"' && p[l - 1] == '"') {
			p[l - 1] = 0;
			p++;
		}
		r = XsHostExists(p, NULL) != 0;
		break;
	}
	case BK_DEF:
		r = VarGet(a) != NULL;
		break;
	case BK_ERR:
		r = g_status >= (int)ParseI64(a, NULL);
		break;
	default: {
		BatExpand(n->w[1], b, (int)sizeof b);
		bool an, bn;
		int64_t x = ParseI64(a, &an), y = ParseI64(b, &bn);
		int c;
		if (an && bn)
			c = x < y ? -1 : (x > y ? 1 : 0);
		else
			c = (n->flag & 2) ? (EqCI(a, b) ? 0 : strcmp(a, b)) : strcmp(a, b);
		switch (n->n) {
		case CMP_EQ:
			r = c == 0;
			break;
		case CMP_NE:
			r = c != 0;
			break;
		case CMP_LT:
			r = c < 0;
			break;
		case CMP_LE:
			r = c <= 0;
			break;
		case CMP_GT:
			r = c > 0;
			break;
		case CMP_GE:
			r = c >= 0;
			break;
		}
	}
	}
	if (n->flag & 1)
		r = !r;
	if (r)
		ExecList(n->b);
	else if (n->c)
		ExecList(n->c);
	return g_status;
}

static int ExecBFor(Node* n) {
	char items[XS_FIELD_MAX];
	BatExpand(n->w[0], items, (int)sizeof items);
	int st = 0;
	char num[24];
	if (n->flag) { /* /L (start,step,end) */
		int64_t v[3] = {0, 1, 0};
		int k = 0;
		char* p = items;
		while (*p && k < 3) {
			char* e = p;
			while (*e && *e != ',' && *e != ' ')
				e++;
			char save = *e;
			*e = 0;
			v[k++] = ParseI64(p, NULL);
			if (!save)
				break;
			p = e + 1;
		}
		int64_t step = v[1];
		if (step == 0)
			return 1;
		g_loopdepth++;
		for (int64_t i = v[0]; step > 0 ? i <= v[2] : i >= v[2]; i += step) {
			FmtI64(i, num);
			VarSet(n->s, num);
			st = ExecList(n->b);
			if (g_flow)
				break;
			if (g_interrupted) {
				g_flow = FL_EXIT;
				break;
			}
		}
		g_loopdepth--;
	} else {
		char* p = items;
		g_loopdepth++;
		while (*p) {
			while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t')
				p++;
			if (!*p)
				break;
			char* e = p;
			while (*e && *e != ' ' && *e != ',' && *e != ';' && *e != '\t')
				e++;
			char save = *e;
			*e = 0;
			VarSet(n->s, p);
			st = ExecList(n->b);
			*e = save;
			if (g_flow)
				break;
			if (g_interrupted) {
				g_flow = FL_EXIT;
				break;
			}
			p = e;
		}
		g_loopdepth--;
	}
	if (!g_flow)
		g_status = st;
	return g_status;
}

static int RunProg(Prog* pr, int start) {
	int i = start;
	while (i < pr->nst) {
		if (g_interrupted) {
			g_flow = FL_EXIT;
			break;
		}
		ExecNode(pr->st[i]);
		if (g_flow == FL_GOTO) {
			i = g_flow_n;
			g_flow = FL_NONE;
			continue;
		}
		if (g_flow)
			break;
		i++;
	}
	return g_status;
}

static int XsRunFileInternal(const char* path, int argc, char** argv);

static int BatCall(char* args) {
	char* av[32];
	int n = BatSplit(args, av, 32);
	if (n == 0)
		return 0;
	if (av[0][0] == ':') {
		char up[XS_NAME_MAX];
		size_t i = 0;
		for (; av[0][i + 1] && i < XS_NAME_MAX - 1; i++)
			up[i] = Uc(av[0][i + 1]);
		up[i] = 0;
		int idx = ProgLabel(g_prog, up);
		if (idx < 0) {
			printf("[xesh]: cannot find batch label: %s\r\n", av[0]);
			return 1;
		}
		if (g_fp + 1 >= XS_MAX_FRAMES) {
			printf("[xesh]: call nested too deep\r\n");
			return 1;
		}
		Frame* fr = &g_frames[++g_fp];
		fr->argc = n;
		fr->argv = av;
		fr->shift = 0;
		fr->own = NULL;
		fr->nown = 0;
		RunProg(g_prog, idx);
		if (g_flow == FL_RETURN)
			g_flow = FL_NONE;
		FrameFreeOwn(fr);
		g_fp--;
		return g_status;
	}
	bool bat;
	if (XsScriptKind(av[0], &bat))
		return XsRunFileInternal(av[0], n, av);
	return RunArgs(n, av, true);
}

static int BatSet(char* a) {
	a = TrimL(a);
	if (!*a)
		return 0;
	if (a[0] == '/' && Lc(a[1]) == 'a') {
		bool err;
		ArithEval(TrimL(a + 2), &err);
		if (err)
			printf("[xesh]: bad expression in set /a\r\n");
		return err ? 1 : 0;
	}
	if (a[0] == '/' && Lc(a[1]) == 'p') {
		char* eq = strchr(a, '=');
		if (!eq)
			return 1;
		*eq = 0;
		char* name = TrimL(a + 2);
		char line[XS_FIELD_MAX];
		XsPuts(-1, eq + 1);
		if (!ReadLineIn(line, (int)sizeof line))
			line[0] = 0;
		VarSet(name, line);
		return 0;
	}
	char* eq = strchr(a, '=');
	if (!eq)
		return 1;
	*eq = 0;
	if (eq[1] == 0)
		VarUnset(a);
	else
		VarSet(a, eq + 1);
	return 0;
}

static int BatDispatch(char* p) {
	char word[32];
	int wl = 0;
	while (p[wl] && !IsSp(p[wl]) && p[wl] != '.' && p[wl] != '/' && wl < 31) {
		word[wl] = Lc(p[wl]);
		wl++;
	}
	word[wl] = 0;
	char* rest = TrimL(p + wl);
	if (!strcmp(word, "echo")) {
		if (p[wl] == '.') {
			rest = p + wl + 1;
		} else if (!*rest) {
			XsPuts(g_fd_out, g_bat_echo ? "ECHO is on\n" : "ECHO is off\n");
			return 0;
		} else if (EqCI(rest, "off")) {
			g_bat_echo = false;
			return 0;
		} else if (EqCI(rest, "on")) {
			g_bat_echo = true;
			return 0;
		}
		XsPuts(g_fd_out, rest);
		XsPuts(g_fd_out, "\n");
		return 0;
	}
	if (!strcmp(word, "set"))
		return BatSet(rest);
	if (!strcmp(word, "setlocal") || !strcmp(word, "endlocal") || !strcmp(word, "title"))
		return 0;
	if (!strcmp(word, "shift")) {
		if (g_fp >= 0 && g_frames[g_fp].shift < g_frames[g_fp].argc - 1)
			g_frames[g_fp].shift++;
		return 0;
	}
	if (!strcmp(word, "call"))
		return BatCall(rest);
	if (!strcmp(word, "exit")) {
		char* e = rest;
		bool b = (e[0] == '/' && Lc(e[1]) == 'b');
		if (b)
			e = TrimL(e + 2);
		int code = *e ? (int)ParseI64(e, NULL) : g_status;
		g_flow = b ? FL_RETURN : FL_EXIT;
		return code;
	}
	if (!strcmp(word, "pause")) {
		XsPuts(g_fd_out, "Press any key to continue . . . ");
		XsHostReadKey();
		XsPuts(g_fd_out, "\n");
		return 0;
	}
	if (!strcmp(word, "timeout")) { /* timeout /t N [/nobreak] */
		char* av[8];
		int an = BatSplit(p, av, 8);
		for (int i = 1; i + 1 < an; i++)
			if (av[i][0] == '/' && Lc(av[i][1]) == 't') {
				XsHostSleepMs((int)ParseI64(av[i + 1], NULL) * 1000);
				return 0;
			}
		return 0;
	}
	char* av[32];
	int n = BatSplit(p, av, 32);
	if (n == 0)
		return 0;
	if (EqCI(av[0], "cls"))
		av[0] = (char*)"clrscr";
	if (EqCI(av[0], "chdir"))
		av[0] = (char*)"cd";
	if (EqCI(av[0], "cd") && n > 2 && EqCI(av[1], "/d")) {
		av[1] = av[2];
		n = 2;
	}
	bool bat;
	if (XsScriptKind(av[0], &bat))
		return XsRunFileInternal(av[0], n, av);
	return RunArgs(n, av, true);
}

/* split trailing redirections off a bat command line (quote-aware) */
static int BatExtractRedirs(char* line, Redir* out, int max) {
	int n = 0;
	bool q = false;
	char* first = NULL;
	for (char* s = line; *s; s++) {
		if (*s == '"')
			q = !q;
		else if (!q && (*s == '<' || *s == '>')) {
			first = s;
			if (s > line && IsDig(s[-1]) && (s - 1 == line || IsSp(s[-2])))
				first = s - 1;
			break;
		}
	}
	if (!first)
		return 0;
	char* s = first;
	while (*s && n < max) {
		s = TrimL(s);
		if (!*s)
			break;
		int fd = -1;
		if (IsDig(*s) && (s[1] == '<' || s[1] == '>')) {
			fd = *s - '0';
			s++;
		}
		if (*s != '<' && *s != '>')
			break;
		Redir* r = &out[n];
		memset(r, 0, sizeof *r);
		r->raw = true;
		if (*s == '<') {
			r->op = R_IN;
			r->fd = fd < 0 ? 0 : fd;
			s++;
		} else {
			r->fd = fd < 0 ? 1 : fd;
			s++;
			if (*s == '>') {
				r->op = R_APP;
				s++;
			} else if (*s == '&') {
				r->op = R_DUP;
				s++;
			} else
				r->op = R_OUT;
		}
		s = TrimL(s);
		r->target = s;
		while (*s && !IsSp(*s))
			s++;
		if (*s) {
			*s = 0;
			s++;
		}
		if (n > 0)
			out[n - 1].next = r;
		n++;
	}
	*first = 0;
	return n;
}

static int ExecBCmd(Node* n) {
	char line[XS_FIELD_MAX];
	BatExpand(n->s, line, (int)sizeof line);
	if (g_bat_echo && !n->flag) {
		XsPuts(-1, "> ");
		XsPuts(-1, line);
		XsPuts(-1, "\r\n");
	}
	Redir rd[4];
	int nr = BatExtractRedirs(line, rd, 4);
	IoSave sv;
	sv.no = 0;
	if (nr > 0) {
		if (ApplyRedirs(&rd[0], &sv) < 0) {
			g_status = 1;
			return 1;
		}
	}
	g_status = BatDispatch(TrimL(line));
	if (nr > 0)
		RestoreIo(&sv);
	return g_status;
}

/* ---- the dispatcher ---- */
static int ExecList(Node* n) {
	for (; n; n = n->next) {
		if (g_interrupted) {
			g_flow = FL_EXIT;
			break;
		}
		ExecNode(n);
		if (g_flow)
			break;
	}
	return g_status;
}

static int ExecNode(Node* n) {
	if (!n)
		return g_status;
	if (g_depth >= XS_MAX_DEPTH) {
		printf("[xesh]: script nesting too deep\r\n");
		g_flow = FL_EXIT;
		g_status = 1;
		return 1;
	}
	g_depth++;
	IoSave sv;
	bool red = n->redir != NULL;
	if (red && ApplyRedirs(n->redir, &sv) < 0) {
		g_status = 1;
		g_depth--;
		return 1;
	}
	switch (n->type) {
	case N_CMD:
		ExecSimple(n);
		break;
	case N_AND:
		ExecNode(n->a);
		if (!g_flow && g_status == 0)
			ExecNode(n->b);
		break;
	case N_OR:
		ExecNode(n->a);
		if (!g_flow && g_status != 0)
			ExecNode(n->b);
		break;
	case N_NOT:
		ExecNode(n->a);
		if (!g_flow)
			g_status = !g_status;
		break;
	case N_PIPE:
		ExecPipe(n);
		break;
	case N_IF:
		ExecList(n->a);
		if (!g_flow) {
			if (g_status == 0)
				ExecList(n->b);
			else if (n->c)
				ExecList(n->c);
			else
				g_status = 0;
		}
		break;
	case N_WHILE:
		ExecWhile(n);
		break;
	case N_FOR:
		ExecFor(n);
		break;
	case N_SELECT:
		ExecSelect(n);
		break;
	case N_CASE:
		ExecCase(n);
		break;
	case N_GROUP:
		ExecList(n->b);
		break;
	case N_FUNC:
		FuncSet(n->s, n->b, g_cur_arena);
		g_status = 0;
		break;
	case N_BCMD:
		ExecBCmd(n);
		break;
	case N_BIF:
		ExecBIf(n);
		break;
	case N_BFOR:
		ExecBFor(n);
		break;
	case N_GOTO: {
		if (!strcmp(n->s, "EOF") || !n->s[0]) {
			g_flow = FL_RETURN;
			break;
		}
		int idx = ProgLabel(g_prog, n->s);
		if (idx < 0) {
			printf("[xesh]: cannot find batch label: %s\r\n", n->s);
			g_flow = FL_EXIT;
			g_status = 1;
		} else {
			g_flow = FL_GOTO;
			g_flow_n = idx;
		}
		break;
	}
	}
	if (red)
		RestoreIo(&sv);
	g_depth--;
	return g_status;
}

/* ------------------------------------------------------------------ */
/* builtins                                                            */
/* ------------------------------------------------------------------ */
static int B_colon(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return 0;
}
static int B_true(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return 0;
}
static int B_false(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return 1;
}
static int B_trap(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return 0;
}

static int B_echo(int argc, char** argv) {
	bool nl = true, esc = false;
	int i = 1;
	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		const char* f = argv[i] + 1;
		bool ok = true;
		for (; *f; f++)
			if (*f != 'n' && *f != 'e' && *f != 'E')
				ok = false;
		if (!ok)
			break;
		for (f = argv[i] + 1; *f; f++) {
			if (*f == 'n')
				nl = false;
			else if (*f == 'e')
				esc = true;
			else
				esc = false;
		}
	}
	Ob ob;
	ob.n = 0;
	ob.fd = g_fd_out;
	for (; i < argc; i++) {
		if (!esc)
			ObStr(&ob, argv[i]);
		else {
			for (const char* s = argv[i]; *s; s++) {
				if (*s == '\\' && s[1]) {
					s++;
					if (*s == 'c') {
						ObFlush(&ob);
						return 0;
					}
					int ch = EscSeq(&s);
					if (ch)
						ObPut(&ob, (char)ch);
				} else
					ObPut(&ob, *s);
			}
		}
		if (i + 1 < argc)
			ObPut(&ob, ' ');
	}
	if (nl)
		ObPut(&ob, '\n');
	ObFlush(&ob);
	return 0;
}

static void PadOut(Ob* o, const char* s, int len, int width, bool left) {
	int pad = width > len ? width - len : 0;
	if (!left)
		while (pad-- > 0)
			ObPut(o, ' ');
	for (int i = 0; i < len; i++)
		ObPut(o, s[i]);
	if (left)
		while (pad-- > 0)
			ObPut(o, ' ');
}

/* printf FORMAT [ARGS...]: %s %d %i %c %x %X %o %u %b %%, flags '-', width */
static int B_printf(int argc, char** argv) {
	if (argc < 2)
		return 1;
	const char* fmt = argv[1];
	int ai = 2;
	Ob ob;
	ob.n = 0;
	ob.fd = g_fd_out;
	do {
		bool used = false;
		for (const char* s = fmt; *s; s++) {
			char c = *s;
			if (c == '\\' && s[1]) {
				s++;
				int ch = EscSeq(&s);
				if (ch)
					ObPut(&ob, (char)ch);
			} else if (c == '%' && s[1]) {
				s++;
				bool left = false;
				int width = 0;
				while (*s == '-' || *s == '0' || *s == '+' || *s == ' ') {
					if (*s == '-')
						left = true;
					s++;
				}
				while (IsDig(*s))
					width = width * 10 + (*s++ - '0');
				if (*s == '.') {
					s++;
					while (IsDig(*s))
						s++;
				}
				const char* a = ai < argc ? argv[ai] : "";
				char b[64];
				if (*s == '%') {
					ObPut(&ob, '%');
				} else if (*s == 's') {
					PadOut(&ob, a, (int)strlen(a), width, left);
					ai++;
					used = true;
				} else if (*s == 'b') {
					char tmp[XS_FIELD_MAX];
					int o = 0;
					for (const char* t = a; *t && o < (int)sizeof tmp - 1; t++) {
						if (*t == '\\' && t[1]) {
							t++;
							int ch = EscSeq(&t);
							if (ch)
								tmp[o++] = (char)ch;
						} else
							tmp[o++] = *t;
					}
					PadOut(&ob, tmp, o, width, left);
					ai++;
					used = true;
				} else if (*s == 'd' || *s == 'i') {
					int l = FmtI64(ParseI64(a, NULL), b);
					PadOut(&ob, b, l, width, left);
					ai++;
					used = true;
				} else if (*s == 'u') {
					int l = FmtI64((int64_t)(uint64_t)ParseI64(a, NULL), b);
					PadOut(&ob, b, l, width, left);
					ai++;
					used = true;
				} else if (*s == 'c') {
					PadOut(&ob, a, *a ? 1 : 0, width, left);
					ai++;
					used = true;
				} else if (*s == 'x' || *s == 'X' || *s == 'o') {
					const char* dg = (*s == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
					unsigned base = (*s == 'o') ? 8 : 16;
					uint64_t v = (uint64_t)ParseI64(a, NULL);
					char t[24];
					int n = 0;
					do {
						t[n++] = dg[v % base];
						v /= base;
					} while (v);
					for (int k = 0; k < n; k++)
						b[k] = t[n - 1 - k];
					PadOut(&ob, b, n, width, left);
					ai++;
					used = true;
				} else if (*s == 0)
					break;
			} else
				ObPut(&ob, c);
		}
		if (!used)
			break;
	} while (ai < argc);
	ObFlush(&ob);
	return 0;
}

static int B_pwd(int argc, char** argv) {
	(void)argc;
	(void)argv;
	XsPuts(g_fd_out, XsHostCwd());
	XsPuts(g_fd_out, "\n");
	return 0;
}

static int B_exit(int argc, char** argv) {
	int code = argc > 1 ? (int)ParseI64(argv[1], NULL) : g_status;
	g_flow = FL_EXIT;
	return code;
}
static int B_return(int argc, char** argv) {
	int code = argc > 1 ? (int)ParseI64(argv[1], NULL) : g_status;
	g_flow = FL_RETURN;
	return code;
}
static int B_break(int argc, char** argv) {
	if (g_loopdepth == 0)
		return 0;
	int n = argc > 1 ? (int)ParseI64(argv[1], NULL) : 1;
	g_flow = FL_BREAK;
	g_flow_n = n < 1 ? 1 : n;
	return 0;
}
static int B_continue(int argc, char** argv) {
	if (g_loopdepth == 0)
		return 0;
	int n = argc > 1 ? (int)ParseI64(argv[1], NULL) : 1;
	g_flow = FL_CONT;
	g_flow_n = n < 1 ? 1 : n;
	return 0;
}

static int B_shift(int argc, char** argv) {
	int n = argc > 1 ? (int)ParseI64(argv[1], NULL) : 1;
	if (g_fp < 0)
		return 1;
	Frame* f = &g_frames[g_fp];
	if (n > PosCount())
		return 1;
	f->shift += n;
	return 0;
}

static int B_set(int argc, char** argv) {
	if (argc >= 2 && !strcmp(argv[1], "--")) {
		if (g_fp < 0)
			return 1;
		Frame* f = &g_frames[g_fp];
		int n = argc - 2;
		char** own = (char**)malloc((size_t)(n + 1) * sizeof(char*));
		if (!own)
			return 1;
		own[0] = f->argc > 0 ? f->argv[0] : (char*)"";
		for (int i = 0; i < n; i++) {
			size_t l = strlen(argv[2 + i]);
			own[1 + i] = (char*)malloc(l + 1);
			if (own[1 + i])
				memcpy(own[1 + i], argv[2 + i], l + 1);
		}
		FrameFreeOwn(f);
		f->own = own;
		f->nown = n + 1;
		f->argv = own;
		f->argc = n + 1;
		f->shift = 0;
	}
	return 0; /* set -e / -x / -u are accepted and ignored */
}

static int B_export(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		char* eq = strchr(argv[i], '=');
		char name[XS_NAME_MAX];
		size_t nl = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
		if (nl == 0 || nl >= XS_NAME_MAX)
			continue;
		memcpy(name, argv[i], nl);
		name[nl] = 0;
		if (eq)
			VarSet(name, eq + 1);
		Var* v = VarSlot(name, true);
		if (v) {
			v->exported = 1;
			XsHostSetEnv(v->name, v->val);
		}
	}
	return 0;
}
static int B_local(int argc, char** argv) { /* no scoping: behaves like plain assignment */
	for (int i = 1; i < argc; i++) {
		char* eq = strchr(argv[i], '=');
		if (eq) {
			*eq = 0;
			VarSet(argv[i], eq + 1);
			*eq = '=';
		} else
			VarSet(argv[i], "");
	}
	return 0;
}
static int B_unset(int argc, char** argv) {
	for (int i = 1; i < argc; i++)
		VarUnset(argv[i]);
	return 0;
}

/* read [-r] [-s] [-n N] [-p prompt] [-t secs(ignored)] [name...]
 * -n N reads N raw bytes with no line buffering (-s: no echo): this is the
 * primitive a keyboard-driven menu is built on. */
static int B_read(int argc, char** argv) {
	int i = 1;
	const char* prompt = NULL;
	int nchars = 0;
	bool silent = false;
	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			prompt = argv[++i];
			continue;
		}
		if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			i++;
			continue;
		}
		for (const char* f = argv[i] + 1; *f; f++) {
			if (*f == 's')
				silent = true;
			else if (*f == 'n') {
				if (f[1])
					nchars = atoi(f + 1);
				else if (i + 1 < argc)
					nchars = atoi(argv[++i]);
				break;
			}
		}
	}
	if (prompt)
		XsPuts(-1, prompt);
	char line[XS_FIELD_MAX];
	if (nchars > 0) {
		int n = 0;
		bool eof = false;
		while (n < nchars && n < (int)sizeof line - 1) {
			int c = ReadKeyIn();
			if (c < 0) {
				eof = true;
				break;
			}
			if (c == '\n' || c == '\r')
				break;
			line[n++] = (char)c;
			if (!silent && StdinIsConsole() && c >= 32) {
				char ch = (char)c;
				XsWrite(-1, &ch, 1);
			}
		}
		line[n] = 0;
		if (eof && n == 0)
			return 1;
	} else if (!ReadLineIn(line, (int)sizeof line))
		return 1;

	if (i >= argc) {
		VarSet("REPLY", line);
		return 0;
	}
	if (nchars > 0) { /* raw read: the whole text goes to the first name */
		VarSet(argv[i], line);
		for (i++; i < argc; i++)
			VarSet(argv[i], "");
		return 0;
	}
	char* p = line;
	for (; i < argc; i++) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (i == argc - 1) {
			VarSet(argv[i], p);
			break;
		}
		char* e = p;
		while (*e && *e != ' ' && *e != '\t')
			e++;
		char s = *e;
		*e = 0;
		VarSet(argv[i], p);
		if (!s) {
			p = e;
			for (i++; i < argc; i++)
				VarSet(argv[i], "");
			break;
		}
		p = e + 1;
	}
	return 0;
}

static int B_let(int argc, char** argv) {
	int64_t v = 0;
	for (int i = 1; i < argc; i++) {
		bool err;
		v = ArithEval(argv[i], &err);
		if (err) {
			printf("[xesh]: let: bad expression: %s\r\n", argv[i]);
			return 1;
		}
	}
	return v == 0;
}
static int B_expr(int argc, char** argv) {
	char buf[XS_FIELD_MAX];
	int o = 0;
	for (int i = 1; i < argc; i++) {
		for (const char* s = argv[i]; *s && o < (int)sizeof buf - 2; s++)
			buf[o++] = *s;
		buf[o++] = ' ';
	}
	buf[o] = 0;
	bool err;
	int64_t v = ArithEval(buf, &err);
	if (err) {
		printf("[xesh]: expr: syntax error\r\n");
		return 2;
	}
	char b[24];
	FmtI64(v, b);
	XsPuts(g_fd_out, b);
	XsPuts(g_fd_out, "\n");
	return v == 0;
}

static int B_eval(int argc, char** argv) {
	char buf[XS_FIELD_MAX];
	int o = 0;
	for (int i = 1; i < argc; i++) {
		for (const char* s = argv[i]; *s && o < (int)sizeof buf - 2; s++)
			buf[o++] = *s;
		if (i + 1 < argc)
			buf[o++] = ' ';
	}
	buf[o] = 0;
	return ExecString(buf, o, true);
}

/* sleep N  (seconds, optional fraction: sleep 0.2) */
static int B_sleep(int argc, char** argv) {
	if (argc < 2)
		return 1;
	const char* s = argv[1];
	int ms = 0;
	while (IsDig(*s))
		ms = ms * 10 + (*s++ - '0');
	ms *= 1000;
	if (*s == '.') {
		s++;
		int scale = 100;
		while (IsDig(*s) && scale) {
			ms += (*s++ - '0') * scale;
			scale /= 10;
		}
	}
	/* sleep in slices so Ctrl+C still lands */
	while (ms > 0 && !g_interrupted) {
		int slice = ms > 50 ? 50 : ms;
		XsHostSleepMs(slice);
		ms -= slice;
	}
	return 0;
}

static int B_clear(int argc, char** argv) {
	(void)argc;
	(void)argv;
	XsPuts(g_fd_out, "\033[2J\033[H");
	return 0;
}

/* tput cup R C | clear | civis | cnorm | sgr0 | bold | rev | setaf N | setab N | cols | lines */
static int B_tput(int argc, char** argv) {
	if (argc < 2)
		return 1;
	const char* c = argv[1];
	char b[64];
	int fgmap[8] = {30, 31, 32, 33, 34, 35, 36, 37};
	if (!strcmp(c, "cup") && argc >= 4) {
		int r = (int)ParseI64(argv[2], NULL) + 1, col = (int)ParseI64(argv[3], NULL) + 1;
		char rb[24], cb[24];
		FmtI64(r, rb);
		FmtI64(col, cb);
		snprintf(b, sizeof b, "\033[%s;%sH", rb, cb);
		XsPuts(g_fd_out, b);
	} else if (!strcmp(c, "clear"))
		XsPuts(g_fd_out, "\033[2J\033[H");
	else if (!strcmp(c, "civis"))
		XsPuts(g_fd_out, "\033[?25l");
	else if (!strcmp(c, "cnorm"))
		XsPuts(g_fd_out, "\033[?25h");
	else if (!strcmp(c, "sgr0"))
		XsPuts(g_fd_out, "\033[0m");
	else if (!strcmp(c, "bold"))
		XsPuts(g_fd_out, "\033[1m");
	else if (!strcmp(c, "rev"))
		XsPuts(g_fd_out, "\033[7m");
	else if ((!strcmp(c, "setaf") || !strcmp(c, "setab")) && argc >= 3) {
		int n = (int)ParseI64(argv[2], NULL) & 7;
		snprintf(b, sizeof b, "\033[%dm", fgmap[n] + (c[3] == 'b' ? 10 : 0));
		XsPuts(g_fd_out, b);
	} else if (!strcmp(c, "cols") || !strcmp(c, "lines")) {
		int cols = 80, rows = 24;
		XsHostWinSize(&cols, &rows);
		FmtI64(c[0] == 'c' ? cols : rows, b);
		XsPuts(g_fd_out, b);
		XsPuts(g_fd_out, "\n");
	} else
		return 1;
	return 0;
}

/* test / [ */
typedef struct {
	int argc;
	char** argv;
	int i;
	bool err;
} Ts;
static bool TsOr(Ts* t);

static bool TsIsBin(const char* s) {
	return !strcmp(s, "=") || !strcmp(s, "==") || !strcmp(s, "!=") || !strcmp(s, "-eq") ||
		   !strcmp(s, "-ne") || !strcmp(s, "-lt") || !strcmp(s, "-le") || !strcmp(s, "-gt") ||
		   !strcmp(s, "-ge");
}

static bool TsPrimary(Ts* t) {
	if (t->i >= t->argc) {
		t->err = true;
		return false;
	}
	const char* a = t->argv[t->i];
	if (!strcmp(a, "(")) {
		t->i++;
		bool v = TsOr(t);
		if (t->i < t->argc && !strcmp(t->argv[t->i], ")"))
			t->i++;
		else
			t->err = true;
		return v;
	}
	if (t->i + 2 < t->argc && TsIsBin(t->argv[t->i + 1])) {
		const char* l = a;
		const char* op = t->argv[t->i + 1];
		const char* r = t->argv[t->i + 2];
		t->i += 3;
		if (!strcmp(op, "=") || !strcmp(op, "=="))
			return strcmp(l, r) == 0;
		if (!strcmp(op, "!="))
			return strcmp(l, r) != 0;
		int64_t x = ParseI64(l, NULL), y = ParseI64(r, NULL);
		if (!strcmp(op, "-eq"))
			return x == y;
		if (!strcmp(op, "-ne"))
			return x != y;
		if (!strcmp(op, "-lt"))
			return x < y;
		if (!strcmp(op, "-le"))
			return x <= y;
		if (!strcmp(op, "-gt"))
			return x > y;
		return x >= y;
	}
	if (a[0] == '-' && a[1] && !a[2] && t->i + 1 < t->argc) {
		const char* v = t->argv[t->i + 1];
		char c = a[1];
		if (c == 'z' || c == 'n' || c == 'e' || c == 'f' || c == 'd' || c == 'r' || c == 'w' ||
			c == 'x' || c == 's') {
			t->i += 2;
			if (c == 'z')
				return v[0] == 0;
			if (c == 'n')
				return v[0] != 0;
			int isDir = 0;
			int ex = XsHostExists(v, &isDir);
			if (c == 'd')
				return ex && isDir;
			if (c == 'f')
				return ex && !isDir;
			return ex != 0;
		}
	}
	t->i++;
	return a[0] != 0;
}
static bool TsNot(Ts* t) {
	if (t->i < t->argc && !strcmp(t->argv[t->i], "!")) {
		t->i++;
		return !TsNot(t);
	}
	return TsPrimary(t);
}
static bool TsAnd(Ts* t) {
	bool v = TsNot(t);
	while (t->i < t->argc && !strcmp(t->argv[t->i], "-a")) {
		t->i++;
		bool r = TsNot(t);
		v = v && r;
	}
	return v;
}
static bool TsOr(Ts* t) {
	bool v = TsAnd(t);
	while (t->i < t->argc && !strcmp(t->argv[t->i], "-o")) {
		t->i++;
		bool r = TsAnd(t);
		v = v || r;
	}
	return v;
}
static int B_test(int argc, char** argv) {
	Ts t = {argc - 1, argv + 1, 0, false};
	if (t.argc == 0)
		return 1;
	bool v = TsOr(&t);
	if (t.err || t.i != t.argc) {
		printf("[xesh]: test: bad expression\r\n");
		return 2;
	}
	return v ? 0 : 1;
}
static int B_bracket(int argc, char** argv) {
	if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
		printf("[xesh]: [: missing ']'\r\n");
		return 2;
	}
	return B_test(argc - 1, argv);
}

static int B_source(int argc, char** argv) {
	if (argc < 2) {
		printf("[xesh]: source: filename required\r\n");
		return 2;
	}
	return XsRunFileInternal(argv[1], argc - 1, argv + 1);
}

/* sorted by strcmp so lookup is a binary search: O(log B) */
static const BuiltinDef g_builtins[] = {
	{".", B_source},	{":", B_colon},			  {"[", B_bracket},		{"break", B_break},
	{"clear", B_clear}, {"continue", B_continue}, {"echo", B_echo},		{"eval", B_eval},
	{"exit", B_exit},	{"export", B_export},	  {"expr", B_expr},		{"false", B_false},
	{"let", B_let},		{"local", B_local},		  {"printf", B_printf}, {"pwd", B_pwd},
	{"read", B_read},	{"return", B_return},	  {"set", B_set},		{"shift", B_shift},
	{"sleep", B_sleep}, {"source", B_source},	  {"test", B_test},		{"tput", B_tput},
	{"trap", B_trap},	{"true", B_true},		  {"unset", B_unset},
};
#define XS_NBUILTINS ((int)(sizeof(g_builtins) / sizeof(g_builtins[0])))

static const BuiltinDef* BuiltinFind(const char* name) {
	int lo = 0, hi = XS_NBUILTINS - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		int c = strcmp(name, g_builtins[mid].name);
		if (c == 0)
			return &g_builtins[mid];
		if (c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return NULL;
}

#ifdef XS_TEST
extern "C" int XsBuiltinsSorted(void) {
	for (int i = 1; i < XS_NBUILTINS; i++)
		if (strcmp(g_builtins[i - 1].name, g_builtins[i].name) >= 0)
			return 0;
	return 1;
}
#endif

/* ------------------------------------------------------------------ */
/* ExecString (eval, $(...))                                           */
/* ------------------------------------------------------------------ */
static int ExecString(const char* text, int len, bool propagate) {
	Parser* P = ParserNew(text, len);
	if (!P)
		return 1;
	XsArena ar = {NULL};
	P->ar = &ar;
	Next(P);
	Node* l = ParseList(P);
	bool ok = !P->err && P->tok == T_EOF;
	if (!ok && !P->err)
		PErr(P, "syntax error: unexpected token");
	int st = 1;
	if (ok) {
		XsArena* sa = g_cur_arena;
		g_cur_arena = &ar;
		ExecList(l);
		st = g_status;
		FuncPurge(&ar);
		g_cur_arena = sa;
		if (!propagate && g_flow != FL_NONE && g_flow != FL_EXIT)
			g_flow = FL_NONE;
		if (!propagate && g_flow == FL_EXIT && !g_interrupted)
			g_flow = FL_NONE;
	}
	ArenaFree(&ar);
	free(P);
	return st;
}

/* ------------------------------------------------------------------ */
/* entry points                                                        */
/* ------------------------------------------------------------------ */
bool XsScriptKind(const char* name, bool* isBat) {
	size_t l = strlen(name);
	if (l >= 3 && EqCI(name + l - 3, ".sh")) {
		if (isBat)
			*isBat = false;
		return true;
	}
	if (l >= 4 && (EqCI(name + l - 4, ".bat") || EqCI(name + l - 4, ".cmd"))) {
		if (isBat)
			*isBat = true;
		return true;
	}
	return false;
}

static int RunTextEx(const char* text, int len, bool bat, int argc, char** argv) {
	if (g_scriptdepth >= XS_MAX_NEST) {
		printf("[xesh]: scripts nested too deep\r\n");
		return 1;
	}
	if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
		(unsigned char)text[2] == 0xBF) {
		text += 3;
		len -= 3;
	}
	Parser* P = ParserNew(text, len);
	if (!P)
		return 1;
	XsArena ar = {NULL};
	P->ar = &ar;
	Node* root = NULL;
	Prog prog;
	memset(&prog, 0, sizeof prog);
	bool ok;
	if (bat) {
		ok = ParseBat(P, &prog);
	} else {
		Next(P);
		root = ParseList(P);
		ok = !P->err;
		if (ok && P->tok != T_EOF) {
			PErr(P, "syntax error: unexpected token");
			ok = false;
		}
	}
	if (!ok) {
		ArenaFree(&ar);
		free(P);
		return 2;
	}
	if (g_fp + 1 >= XS_MAX_FRAMES) {
		printf("[xesh]: scripts nested too deep\r\n");
		ArenaFree(&ar);
		free(P);
		return 1;
	}

	Prog* sprog = g_prog;
	XsArena* sar = g_cur_arena;
	bool sbat = g_bat_mode, secho = g_bat_echo;
	int sfd[3] = {g_fd_in, g_fd_out, g_fd_err};
	int sld = g_loopdepth;
	g_prog = &prog;
	g_cur_arena = &ar;
	g_bat_mode = bat;
	g_bat_echo = true;
	g_loopdepth = 0;
	g_flow = FL_NONE;

	Frame* fr = &g_frames[++g_fp];
	fr->argc = argc;
	fr->argv = argv;
	fr->shift = 0;
	fr->own = NULL;
	fr->nown = 0;

	g_scriptdepth++;
	if (bat)
		RunProg(&prog, 0);
	else
		ExecList(root);
	g_scriptdepth--;

	int st = g_status;
	g_flow = FL_NONE;
	FrameFreeOwn(fr);
	g_fp--;
	FuncPurge(&ar);
	g_prog = sprog;
	g_cur_arena = sar;
	g_bat_mode = sbat;
	g_bat_echo = secho;
	g_fd_in = sfd[0];
	g_fd_out = sfd[1];
	g_fd_err = sfd[2];
	g_loopdepth = sld;
	ArenaFree(&ar);
	free(P);
	if (g_scriptdepth == 0 && g_interrupted) {
		printf("[xesh]: script interrupted\r\n");
		g_interrupted = 0;
	}
	fflush(stdout);
	return st;
}

int XsRunText(const char* text, int len, bool bat, int argc, char** argv) {
	return RunTextEx(text, len, bat, argc, argv);
}

static int XsRunFileInternal(const char* path, int argc, char** argv) {
	bool bat = false;
	XsScriptKind(path, &bat);
	char full[512];
	if (path[0] == '/' || (IsAlpha(path[0]) && path[1] == ':')) {
		strncpy(full, path, sizeof full - 1);
		full[sizeof full - 1] = 0;
	} else {
		const char* cwd = XsHostCwd();
		size_t cl = strlen(cwd);
		if (path[0] == '.' && path[1] == '/')
			path += 2;
		snprintf(full, sizeof full, "%s%s%s", cwd, (cl > 1 && cwd[cl - 1] != '/') ? "/" : "", path);
	}
	int fd = XsHostOpen(full, 0);
	if (fd < 0) {
		printf("[xesh]: script not found: %s\r\n", full);
		return 127;
	}
	size_t cap = 4096, n = 0;
	char* data = (char*)malloc(cap);
	if (!data) {
		XsHostClose(fd);
		return 1;
	}
	for (;;) {
		if (n + 1024 >= cap) {
			if (cap >= XS_MAX_SCRIPT)
				break;
			cap *= 2;
			char* nb = (char*)realloc(data, cap);
			if (!nb)
				break;
			data = nb;
		}
		int got = XsHostRead(fd, data + n, (int)(cap - n - 1));
		if (got <= 0)
			break;
		/* Some filesystems return a whole block past EOF (zero padding or
                 * stale bytes). A script is text, so the first control byte
                 * other than TAB/LF/CR/ESC is treated as the end of the file. */
		int stop = -1;
		for (int k = 0; k < got; k++) {
			unsigned char ch = (unsigned char)data[n + (size_t)k];
			if (ch == 0 || (ch < 32 && ch != '\t' && ch != '\n' && ch != '\r' && ch != 27)) {
				stop = k; /* padding / garbage past EOF */
				break;
			}
		}
		if (stop >= 0) {
			n += (size_t)stop;
			break;
		}
		n += (size_t)got;
	}
	XsHostClose(fd);
	RbInvalidate(fd);
	data[n] = 0;
	int st = RunTextEx(data, (int)n, bat, argc, argv);
	free(data);
	return st;
}

int XsRunFile(const char* path, int argc, char** argv) {
	return XsRunFileInternal(path, argc, argv);
}
