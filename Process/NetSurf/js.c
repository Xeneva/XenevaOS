/**
 * NetSurf QuickJS host implementation. See js.h.
 *
 * One QuickJS runtime/context per page. Inline <script> bodies are fed to
 * NsJsScriptChunk by the HTML pass and evaled at </script>, so document
 * order and JS-side text injection line up with the rendered page. The
 * context outlives the parse so setTimeout/setInterval callbacks keep
 * firing from the GUI loop until the next page replaces it.
 *
 * Host shell. getElementById returns a live node once NsJsSetDocument
 * is called; setAttribute("style") updates that node's inline sheet.
 * Lookups before the DOM exists, and every other DOM call, stay soft:
 *   console.log/info/warn/error/debug   -> styled lines through the sink
 *   document.write/writeln              -> inline text through the sink
 *   document.title                      -> get/set (title scan preloaded)
 *   location.href/replace/assign       -> queued navigation (fetched by the caller)
 *   document.cookie                     -> the shared HTTP cookie jar
 *   navigator.userAgent                 -> same Chrome string the fetch sends
 *   alert(...)                          -> warning line
 *   setTimeout/setInterval/clear*       -> timer slots driven by NsJsTick;
 *                                          callbacks may be functions or
 *                                          source strings
 *   requestAnimationFrame               -> setTimeout(16ms) compat shim
 *   window                              -> alias of globalThis
 */

#include "js.h"
#include "css.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <quickjs.h>
#include <https.h>

#include <_xeneva.h>
#include <sys/_ketime.h>

#define NS_JS_TIMER_MAX 16
#define NS_JS_TIMER_MIN_MS 10
#define NS_JS_RAF_MS 16
#define NS_JS_CONSOLE_MAX 24
#define NS_JS_CONSOLE_LINE 512
#define NS_JS_TITLE_MAX 128

const char* NsJsVersion(void) {
	return "QuickJS 2026-06-04";
}

/* Page scripts stay in the tree. Flip this to run them again. */
static const bool s_jsEnabled = true;

/* Set after hubbub builds the tree. Null while the HTML scan is still
 * running scripts, and again once the next page tears the context down. */
static NsCssNode* s_jsRoot;
static int s_domDirty;

/* Console ring: level + text, drained through the sink. */
typedef struct {
	int kind;
	size_t len;
	char line[NS_JS_CONSOLE_LINE];
} NsJsConsoleEntry;

static NsJsConsoleEntry s_con[NS_JS_CONSOLE_MAX];
static size_t s_conHead = 0, s_conTail = 0; /* head = pop, tail = push */

static NsJsSinkFn* s_sink = NULL;
/* application/ld+json is `{ "a": ... }`, not a statement. Eval reports
 * "expecting ';'" and that line was painted into the page. */
static bool s_scriptNotJs = false;

typedef struct {
	JSValue cb; /* function or source string */
	int64_t due;
	int64_t period; /* 0 = one-shot */
	bool used;
} NsJsTimer;

static NsJsTimer s_timers[NS_JS_TIMER_MAX];

static char s_script[NS_JS_SCRIPT_MAX + 1];
static size_t s_scriptLen = 0;
static bool s_scriptExternal = false;
static char s_srcAttr[128];

static char s_title[NS_JS_TITLE_MAX];
static char s_url[2048];
static char s_nav[2048];
static int s_navPending;
static int s_pumpCapped;

static JSRuntime* s_rt = NULL;
static JSContext* s_ctx = NULL;
static uint64_t s_navMs = 0; /* performance.timeOrigin */

/* ------------------------------------------------------------------ */
/* console ring                                                        */
/* ------------------------------------------------------------------ */

static void NsConsolePush(int kind, const char* msg, size_t len) {
	if (!msg)
		return;
	if (len >= NS_JS_CONSOLE_LINE)
		len = NS_JS_CONSOLE_LINE - 1;
	if (len == 0)
		return;
	size_t next = (s_conTail + 1) % NS_JS_CONSOLE_MAX;
	if (next == s_conHead) /* ring full: drop the oldest line */
		s_conHead = (s_conHead + 1) % NS_JS_CONSOLE_MAX;
	NsJsConsoleEntry* e = &s_con[s_conTail];
	e->kind = kind;
	memcpy(e->line, (void*)msg, len);
	e->line[len] = 0;
	e->len = len;
	s_conTail = next;
}

static size_t NsConsolePending(void) {
	return (s_conTail + NS_JS_CONSOLE_MAX - s_conHead) % NS_JS_CONSOLE_MAX;
}

void NsJsFlushConsole(void) {
	if (!s_sink) {
		s_conHead = s_conTail = 0;
		return;
	}
	while (NsConsolePending() > 0) {
		NsJsConsoleEntry* e = &s_con[s_conHead];
		s_conHead = (s_conHead + 1) % NS_JS_CONSOLE_MAX;
		/* Level prefix + entry + newline in one chunk, so a line keeps
		 * one style no matter how it wraps. */
		const char* pfx = (e->kind == NS_JS_KIND_LOG) ? "$ " : "! ";
		char buf[NS_JS_CONSOLE_LINE];
		size_t pl = strlen(pfx);
		size_t bl = e->len;
		if (pl + bl + 1 > sizeof(buf))
			bl = sizeof(buf) - pl - 1;
		memcpy(buf, pfx, pl);
		memcpy(buf + pl, (void*)e->line, bl);
		buf[pl + bl] = '\n';
		s_sink(buf, pl + bl + 1, e->kind);
	}
}

/* ------------------------------------------------------------------ */
/* JS callouts                                                         */
/* ------------------------------------------------------------------ */

static void NsCons(JSContext* ctx, JSValueConst* argv, int argc, int kind) {
	char line[NS_JS_CONSOLE_LINE];
	size_t len = 0;
	for (int i = 0; i < argc; i++) {
		const char* s = JS_ToCString(ctx, argv[i]);
		if (!s) { /* coercion failure: keep going with the rest */
			JS_FreeValue(ctx, JS_GetException(ctx));
			continue;
		}
		size_t sl = strlen(s);
		if (len && len + sl + 1 < sizeof(line))
			line[len++] = ' ';
		if (len + sl + 1 <= sizeof(line)) {
			memcpy(line + len, (void*)s, sl);
			len += sl;
		}
		JS_FreeCString(ctx, s);
	}
	NsConsolePush(kind, line, len);
}

static JSValue JsConsoleLog(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	NsCons(ctx, argv, argc, NS_JS_KIND_LOG);
	NsJsFlushConsole();
	return JS_UNDEFINED;
}

static JSValue JsConsoleWarn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	NsCons(ctx, argv, argc, NS_JS_KIND_WARN);
	NsJsFlushConsole();
	return JS_UNDEFINED;
}

static JSValue JsConsoleError(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	NsCons(ctx, argv, argc, NS_JS_KIND_ERR);
	NsJsFlushConsole();
	return JS_UNDEFINED;
}

static JSValue JsPerfNow(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	uint64_t now = _KeGetCurrentMS();
	double ms = (now >= s_navMs) ? (double)(now - s_navMs) : 0.0;
	return JS_NewFloat64(ctx, ms);
}

static JSValue JsPerfEntries(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JS_NewArray(ctx);
}

static JSValue JsPerfByType(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	const char* t = NULL;
	JSValue arr;
	(void)this_val;
	if (argc >= 1)
		t = JS_ToCString(ctx, argv[0]);
	arr = JS_NewArray(ctx);
	if (t && !strcmp(t, "navigation")) {
		JSValue ent = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, ent, "type", JS_NewString(ctx, "navigate"));
		JS_SetPropertyStr(ctx, ent, "startTime", JS_NewFloat64(ctx, 0));
		JS_SetPropertyStr(ctx, ent, "duration", JS_NewFloat64(ctx, 20));
		JS_SetPropertyUint32(ctx, arr, 0, ent);
	}
	if (t)
		JS_FreeCString(ctx, t);
	return arr;
}

static JSValue JsSoftNew(JSContext* ctx);

static JSValue JsNop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JsSoftNew(ctx);
}

static JSValue JsRetEmpty(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JS_NewString(ctx, "");
}

static JSValue JsRetTrue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JS_NewBool(ctx, 1);
}

static JSValue JsRetUndefined(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)ctx;
	(void)this_val;
	(void)argc;
	(void)argv;
	return JS_UNDEFINED;
}

/* A stand-in for a DOM node the page assigns to. Missing properties stay
 * undefined: a callable stand-in is truthy, and the search wall then
 * skips the real check. `then` stays undefined so it is not a Promise. */
static JSClassID s_softClass;
static int s_softReady;

static JSValue JsSoftNew(JSContext* ctx);

static JSValue JsSoftGet(JSContext* ctx, JSValueConst obj, JSAtom atom, JSValueConst receiver) {
	const char* name;
	JSValue proto, got;
	JSValue key;
	bool isSymbol;
	(void)receiver;
	/* Never fabricate a Soft for Symbol.toPrimitive/Symbol.iterator/etc:
	 * a callable that returns an object makes every implicit coercion throw
	 * QuickJS's bare "toPrimitive" TypeError. */
	key = JS_AtomToValue(ctx, atom);
	if (JS_IsException(key))
		return key;
	isSymbol = JS_IsSymbol(key);
	JS_FreeValue(ctx, key);
	if (isSymbol)
		return JS_UNDEFINED;
	name = JS_AtomToCString(ctx, atom);
	if (name && (!strcmp(name, "then") || !strcmp(name, "$$typeof") || !strcmp(name, "toJSON"))) {
		JS_FreeCString(ctx, name);
		return JS_UNDEFINED;
	}
	if (name)
		JS_FreeCString(ctx, name);
	proto = JS_GetPrototype(ctx, obj);
	if (!JS_IsUndefined(proto) && !JS_IsNull(proto) && JS_GetClassID(proto) != s_softClass) {
		got = JS_GetProperty(ctx, proto, atom);
		JS_FreeValue(ctx, proto);
		if (!JS_IsException(got) && !JS_IsUndefined(got))
			return got;
		JS_FreeValue(ctx, got);
	} else {
		JS_FreeValue(ctx, proto);
	}
	return JS_UNDEFINED;
}

static JSClassExoticMethods s_softExotic = {
	NULL, NULL, NULL, NULL, NULL, JsSoftGet, NULL, NULL, NULL, NULL, NULL,
};

static JSValue JsSoftNew(JSContext* ctx) {
	JSValue obj;
	if (!s_softReady) {
		JSClassDef def;
		memset(&def, 0, sizeof(def));
		def.class_name = "Soft";
		def.exotic = &s_softExotic;
		if (s_softClass == 0) {
			JSClassID id = JS_NewClassID(&s_softClass);
			if (id == 0)
				return JS_EXCEPTION;
			if (JS_NewClass(JS_GetRuntime(ctx), id, &def) < 0) {
				s_softClass = 0;
				return JS_EXCEPTION;
			}
		}
		/* Soft stands in for DOM nodes and missing window globals, but it
		 * must still coerce like an object: without Object.prototype,
		 * valueOf/toString are missing and any implicit coercion throws
		 * QuickJS's bare "toPrimitive" TypeError. */
		JSValue std = JS_NewObject(ctx);
		if (JS_IsException(std))
			return JS_EXCEPTION;
		JSValue proto = JS_GetPrototype(ctx, std);
		if (JS_IsException(proto)) {
			JS_FreeValue(ctx, std);
			return JS_EXCEPTION;
		}
		JS_SetClassProto(ctx, s_softClass, proto); /* takes ownership */
		JS_FreeValue(ctx, std);
		s_softReady = 1;
	}
	obj = JS_NewObjectClass(ctx, s_softClass);
	if (JS_IsException(obj))
		return obj;
	return obj;
}

/* Pages call getElementById(...).addEventListener and set .onload.
 * A missing node is `undefined`, which is the TypeError in the log.
 * Each lookup returns an object those assignments can land on. */
static JSValue JsMakeEl(JSContext* ctx) {
	JSValue el = JsSoftNew(ctx);
	JSValue style = JS_NewObject(ctx);
	JSValue cl = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, el, "style", style);
	JS_SetPropertyStr(ctx, el, "classList", cl);
	JS_SetPropertyStr(ctx, cl, "add", JS_NewCFunction(ctx, JsNop, "add", 1));
	JS_SetPropertyStr(ctx, cl, "remove", JS_NewCFunction(ctx, JsNop, "remove", 1));
	JS_SetPropertyStr(ctx, cl, "contains", JS_NewCFunction(ctx, JsNop, "contains", 1));
	JS_SetPropertyStr(ctx, el, "addEventListener",
					  JS_NewCFunction(ctx, JsNop, "addEventListener", 2));
	JS_SetPropertyStr(ctx, el, "removeEventListener",
					  JS_NewCFunction(ctx, JsNop, "removeEventListener", 2));
	JS_SetPropertyStr(ctx, el, "setAttribute", JS_NewCFunction(ctx, JsNop, "setAttribute", 2));
	JS_SetPropertyStr(ctx, el, "getAttribute", JS_NewCFunction(ctx, JsRetEmpty, "getAttribute", 1));
	JS_SetPropertyStr(ctx, el, "appendChild", JS_NewCFunction(ctx, JsNop, "appendChild", 1));
	JS_SetPropertyStr(ctx, el, "removeChild", JS_NewCFunction(ctx, JsNop, "removeChild", 1));
	JS_SetPropertyStr(ctx, el, "play", JS_NewCFunction(ctx, JsNop, "play", 0));
	JS_SetPropertyStr(ctx, el, "pause", JS_NewCFunction(ctx, JsNop, "pause", 0));
	JS_SetPropertyStr(ctx, el, "load", JS_NewCFunction(ctx, JsNop, "load", 0));
	JS_SetPropertyStr(ctx, el, "src", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "innerHTML", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "textContent", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "className", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "id", JS_NewString(ctx, ""));
	return el;
}

/* new Image() / new Image(w, h). Search pages construct one before they write. */
static JSValue JsImageCtor(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	JSValue el = JsMakeEl(ctx);
	(void)this_val;
	JS_SetPropertyStr(ctx, el, "width", argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, el, "height", argc >= 2 ? JS_DupValue(ctx, argv[1]) : JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, el, "complete", JS_NewBool(ctx, 1));
	JS_SetPropertyStr(ctx, el, "naturalWidth", JS_NewInt32(ctx, 1));
	JS_SetPropertyStr(ctx, el, "naturalHeight", JS_NewInt32(ctx, 1));
	return el;
}

/* A real element. Soft stand-ins stay for ids that are not in the tree
 * yet (scripts run before the DOM exists). */
static JSClassID s_elClass;

typedef struct JsEl {
	NsCssNode* node;
} JsEl;

static void JsHideNoscript(NsCssNode* n) {
	NsCssNode* c;
	if (!n)
		return;
	if (!n->is_text && !strcmp(n->tag, "noscript"))
		NsCssNodeSetInline(n, "display:none");
	for (c = n->first_child; c; c = c->next)
		JsHideNoscript(c);
}

static int JsNameIs(const char* a, const char* b) {
	size_t n;
	if (!a || !b)
		return 0;
	n = strlen(a);
	return n == strlen(b) && !strncasecmp(a, b, n);
}

static JSValue JsElSetAttribute(JSContext* ctx, JSValueConst this_val, int argc,
								JSValueConst* argv) {
	JsEl* el = JS_GetOpaque(this_val, s_elClass);
	const char* name;
	const char* value;
	if (!el || !el->node || argc < 2)
		return JS_UNDEFINED;
	name = JS_ToCString(ctx, argv[0]);
	value = JS_ToCString(ctx, argv[1]);
	if (!name || !value) {
		if (name)
			JS_FreeCString(ctx, name);
		if (value)
			JS_FreeCString(ctx, value);
		return JS_EXCEPTION;
	}
	if (JsNameIs(name, "style")) {
		NsCssNodeSetInline(el->node, value);
		NsCssNodeAddAttribute(el->node, "style", value);
	} else if (JsNameIs(name, "id")) {
		if (value[0])
			NsCssNodeSetId(el->node, value);
		NsCssNodeAddAttribute(el->node, "id", value);
	} else {
		NsCssNodeAddAttribute(el->node, name, value);
	}
	/* Scripting is running, so the noscript fallback leaves with the
	 * first change. The search wall's timer clears style="display:none"
	 * on the real message two seconds later. */
	JsHideNoscript(s_jsRoot);
	s_domDirty = 1;
	JS_FreeCString(ctx, name);
	JS_FreeCString(ctx, value);
	return JS_UNDEFINED;
}

static JSValue JsElGetAttribute(JSContext* ctx, JSValueConst this_val, int argc,
								JSValueConst* argv) {
	JsEl* el = JS_GetOpaque(this_val, s_elClass);
	const char* name;
	const char* v;
	if (!el || !el->node || argc < 1)
		return JS_NULL;
	name = JS_ToCString(ctx, argv[0]);
	if (!name)
		return JS_EXCEPTION;
	v = NsCssNodeAttr(el->node, name);
	JS_FreeCString(ctx, name);
	if (!v)
		return JS_NULL;
	return JS_NewString(ctx, v);
}

static void JsElFinalizer(JSRuntime* rt, JSValue val) {
	JsEl* el;
	(void)rt;
	el = JS_GetOpaque(val, s_elClass);
	free(el);
}

static int JsEnsureElClass(JSContext* ctx) {
	JSClassDef def;
	JSValue proto;
	if (s_elClass != 0)
		return 0;
	memset(&def, 0, sizeof(def));
	def.class_name = "Element";
	def.finalizer = JsElFinalizer;
	if (JS_NewClassID(&s_elClass) == 0)
		return -1;
	if (JS_NewClass(JS_GetRuntime(ctx), s_elClass, &def) < 0) {
		s_elClass = 0;
		return -1;
	}
	proto = JS_NewObject(ctx);
	if (JS_IsException(proto))
		return -1;
	JS_SetPropertyStr(ctx, proto, "setAttribute",
					  JS_NewCFunction(ctx, JsElSetAttribute, "setAttribute", 2));
	JS_SetPropertyStr(ctx, proto, "getAttribute",
					  JS_NewCFunction(ctx, JsElGetAttribute, "getAttribute", 1));
	JS_SetPropertyStr(ctx, proto, "addEventListener",
					  JS_NewCFunction(ctx, JsRetUndefined, "addEventListener", 2));
	JS_SetPropertyStr(ctx, proto, "removeEventListener",
					  JS_NewCFunction(ctx, JsRetUndefined, "removeEventListener", 2));
	JS_SetClassProto(ctx, s_elClass, proto);
	return 0;
}

static JSValue JsWrapEl(JSContext* ctx, NsCssNode* node) {
	JSValue obj;
	JsEl* el;
	if (JsEnsureElClass(ctx) != 0)
		return JsMakeEl(ctx);
	obj = JS_NewObjectClass(ctx, s_elClass);
	if (JS_IsException(obj))
		return obj;
	el = (JsEl*)malloc(sizeof(JsEl));
	if (!el) {
		JS_FreeValue(ctx, obj);
		return JS_EXCEPTION;
	}
	el->node = node;
	JS_SetOpaque(obj, el);
	JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, node->id));
	return obj;
}

static JSValue JsGetElById(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	const char* id = NULL;
	NsCssNode* node;
	(void)this_val;
	if (argc >= 1)
		id = JS_ToCString(ctx, argv[0]);
	node = (id && s_jsRoot) ? NsCssNodeById(s_jsRoot, id) : NULL;
	if (id)
		JS_FreeCString(ctx, id);
	if (!node)
		return JsMakeEl(ctx);
	return JsWrapEl(ctx, node);
}

static JSValue JsGetEl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JsMakeEl(ctx);
}

static JSValue JsQueryAll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	JSValue arr;
	(void)this_val;
	(void)argc;
	(void)argv;
	arr = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, arr, 0, JsMakeEl(ctx));
	JS_SetPropertyStr(ctx, arr, "length", JS_NewInt32(ctx, 1));
	JS_SetPropertyStr(ctx, arr, "item", JS_NewCFunction(ctx, JsGetEl, "item", 1));
	return arr;
}

static JSValue JsAlert(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	if (argc >= 1) {
		const char* s = JS_ToCString(ctx, argv[0]);
		if (s) {
			NsConsolePush(NS_JS_KIND_WARN, s, strlen(s));
			JS_FreeCString(ctx, s);
			NsJsFlushConsole();
		}
	}
	return JS_UNDEFINED;
}

static JSValue JsDocumentWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	for (int i = 0; i < argc; i++) {
		const char* s = JS_ToCString(ctx, argv[i]);
		if (!s) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			continue;
		}
		if (s_sink)
			s_sink(s, strlen(s), NS_JS_KIND_WRITE);
		JS_FreeCString(ctx, s);
	}
	return JS_UNDEFINED;
}

static JSValue JsDocumentWriteln(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	for (int i = 0; i < argc; i++) {
		const char* s = JS_ToCString(ctx, argv[i]);
		if (!s) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			continue;
		}
		if (s_sink) {
			s_sink(s, strlen(s), NS_JS_KIND_WRITE);
			s_sink("\n", 1, NS_JS_KIND_WRITE);
		}
		JS_FreeCString(ctx, s);
	}
	return JS_UNDEFINED;
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

static void NsTimerClear(int idx) {
	if (idx < 0 || idx >= NS_JS_TIMER_MAX || !s_timers[idx].used)
		return;
	if (s_ctx)
		JS_FreeValue(s_ctx, s_timers[idx].cb);
	s_timers[idx].cb = JS_UNDEFINED;
	s_timers[idx].used = false;
}

int NsJsTimerCount(void) {
	int n = 0;
	for (int i = 0; i < NS_JS_TIMER_MAX; i++)
		if (s_timers[i].used)
			n++;
	return n;
}

static int64_t NsNowMs(void) {
	return (int64_t)_KeGetCurrentMS();
}

/* deadline slot table: ids are slot numbers + 1 */
static JSValue JsSetTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
							bool interval) {
	(void)this_val;
	if (argc < 1)
		return JS_UNDEFINED;
	int slot = -1;
	for (int i = 0; i < NS_JS_TIMER_MAX; i++) {
		if (!s_timers[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		NsConsolePush(NS_JS_KIND_WARN, "timer table full, dropped", 26);
		NsJsFlushConsole();
		return JS_UNDEFINED;
	}
	int64_t ms = 0;
	if (argc >= 2 && JS_ToInt64Ext(ctx, &ms, argv[1]) == 0) {
		if (ms < 0)
			ms = 0;
	}
	if (interval && ms < NS_JS_TIMER_MIN_MS)
		ms = NS_JS_TIMER_MIN_MS;
	NsJsTimer* t = &s_timers[slot];
	t->cb = JS_DupValue(ctx, argv[0]);
	t->period = interval ? ms : 0;
	t->due = NsNowMs() + ms;
	t->used = true;
	return JS_NewInt64(ctx, slot + 1);
}

static JSValue JsTimeoutFn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	return JsSetTimeout(ctx, this_val, argc, argv, false);
}

static JSValue JsIntervalFn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	return JsSetTimeout(ctx, this_val, argc, argv, true);
}

static JSValue JsClearTimer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	int64_t id = -1;
	if (argc >= 1 && JS_ToInt64Ext(ctx, &id, argv[0]) == 0)
		NsTimerClear((int)id - 1);
	return JS_UNDEFINED;
}

static JSValue JsRequestAnimationFrame(JSContext* ctx, JSValueConst this_val, int argc,
									   JSValueConst* argv) {
	/* Shim: an animation request becomes a 16ms timeout. */
	JSValueConst shim[2];
	shim[0] = (argc >= 1) ? argv[0] : JS_UNDEFINED;
	shim[1] = JS_NewInt32(ctx, NS_JS_RAF_MS);
	return JsSetTimeout(ctx, this_val, argc >= 1 ? 2 : 1, shim, false);
}

/* Run one due timer. Callbacks may rearm or clear their own slot; the
 * local cb ref survives either. */
static void NsTimerDispatch(int slot) {
	if (!s_ctx)
		return;
	NsJsTimer t = s_timers[slot];
	JSValue cb = JS_DupValue(s_ctx, t.cb);
	if (t.period == 0)
		NsTimerClear(slot);

	JSValue ret = JS_UNDEFINED;
	if (JS_IsString(cb)) {
		const char* src = JS_ToCString(s_ctx, cb);
		if (src) {
			ret = JS_Eval(s_ctx, src, strlen(src), "<timer>", JS_EVAL_TYPE_GLOBAL);
			JS_FreeCString(s_ctx, src);
		}
	} else {
		ret = JS_Call(s_ctx, cb, JS_UNDEFINED, 0, NULL);
	}
	JS_FreeValue(s_ctx, cb);

	if (JS_IsException(ret)) {
		JSValue ex = JS_GetException(s_ctx);
		const char* msg = JS_ToCString(s_ctx, ex);
		NsConsolePush(NS_JS_KIND_ERR, msg ? msg : "<timer exception>", msg ? strlen(msg) : 17);
		if (msg)
			JS_FreeCString(s_ctx, msg);
		JS_FreeValue(s_ctx, ex);
	}
	JS_FreeValue(s_ctx, ret);
}

static void NsReportException(const char* where);

/* Promise reactions are jobs, not timers. The search challenge sets its
 * cookie from a microtask, so the idle loop has to drain both. */
static int NsJsPump(int budget) {
	int steps = 0;
	if (!s_ctx || !s_rt)
		return 0;
	while (steps < budget) {
		if (JS_IsJobPending(s_rt)) {
			JSContext* pctx = NULL;
			int rc = JS_ExecutePendingJob(s_rt, &pctx);
			if (rc < 0 && s_ctx)
				NsReportException("job");
			steps++;
			continue;
		}
		int slot = -1;
		int64_t now = NsNowMs();
		for (int i = 0; i < NS_JS_TIMER_MAX; i++) {
			if (s_timers[i].used && s_timers[i].due <= now) {
				slot = i;
				break;
			}
		}
		if (slot < 0)
			break;
		NsTimerDispatch(slot);
		if (s_timers[slot].used)
			s_timers[slot].due = now + s_timers[slot].period;
		steps++;
	}
	if (steps >= budget && !s_pumpCapped && s_rt && JS_IsJobPending(s_rt)) {
		s_pumpCapped = 1;
		_KePrint("[js] pump cap\n");
	}
	return steps;
}

bool NsJsTick(void) {
	int steps;
	if (!s_jsEnabled || !s_ctx)
		return false;
	steps = NsJsPump(64);
	if (steps > 0 || NsConsolePending())
		NsJsFlushConsole();
	return steps > 0;
}

int NsJsConsumeNav(char* out, size_t outsz) {
	if (!s_navPending || !out || outsz == 0)
		return 0;
	snprintf(out, outsz, "%s", s_nav);
	s_navPending = 0;
	s_nav[0] = 0;
	return out[0] ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

void NsJsSetSink(NsJsSinkFn* fn) {
	s_sink = fn;
}

static void NsQueueNav(const char* url) {
	if (!url || !url[0])
		return;
	snprintf(s_nav, sizeof(s_nav), "%s", url);
	s_navPending = 1;
	/* The search wall puts a long check value in sg_ss=. Log the length. */
	if (strstr(s_nav, "sg_ss="))
		_KePrint("[js] location %d sg=1\n", (int)strlen(s_nav));
	else
		_KePrint("[js] location %s\n", s_nav);
}

static JSValue JsLocGo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	const char* u;
	(void)this_val;
	if (argc < 1)
		return JS_UNDEFINED;
	u = JS_ToCString(ctx, argv[0]);
	if (!u)
		return JS_EXCEPTION;
	NsQueueNav(u);
	JS_FreeCString(ctx, u);
	return JS_UNDEFINED;
}

static JSValue JsLocHrefGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	(void)this_val;
	(void)argc;
	(void)argv;
	return JS_NewString(ctx, s_url);
}

static JSValue JsLocToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	return JsLocHrefGet(ctx, this_val, argc, argv);
}

static JSValue JsCookieGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	char buf[4096];
	(void)this_val;
	(void)argc;
	(void)argv;
	buf[0] = 0;
	xe_cookie_document(s_url, buf, sizeof(buf));
	return JS_NewString(ctx, buf);
}

static JSValue JsCookieSet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	const char* s;
	(void)this_val;
	if (argc < 1)
		return JS_UNDEFINED;
	s = JS_ToCString(ctx, argv[0]);
	if (!s)
		return JS_EXCEPTION;
	xe_cookie_set(s_url, s, 0);
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static int NsB64Val(int c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

/* Code units 0-255. JS_NewStringLen is UTF-8 and would corrupt a byte >= 128. */
static JSValue NsBytesToString(JSContext* ctx, const unsigned char* bytes, size_t n) {
	size_t i;
	JSValue global, strc, from, parts, out;
	uint32_t part = 0;
	size_t off = 0;
	for (i = 0; i < n; i++) {
		if (bytes[i] >= 128)
			break;
	}
	if (i == n)
		return JS_NewStringLen(ctx, (const char*)bytes, n);
	global = JS_GetGlobalObject(ctx);
	strc = JS_GetPropertyStr(ctx, global, "String");
	from = JS_GetPropertyStr(ctx, strc, "fromCharCode");
	parts = JS_NewArray(ctx);
	out = JS_EXCEPTION;
	while (off < n) {
		size_t m = n - off;
		size_t k;
		JSValue stored[64];
		JSValueConst args[64];
		JSValue piece;
		if (m > 64)
			m = 64;
		for (k = 0; k < m; k++) {
			stored[k] = JS_NewInt32(ctx, bytes[off + k]);
			args[k] = stored[k];
		}
		piece = JS_Call(ctx, from, strc, (int)m, args);
		for (k = 0; k < m; k++)
			JS_FreeValue(ctx, stored[k]);
		if (JS_IsException(piece))
			goto done;
		if (JS_SetPropertyUint32(ctx, parts, part, piece) < 0)
			goto done;
		part++;
		off += m;
	}
	{
		JSValue join = JS_GetPropertyStr(ctx, parts, "join");
		JSValue empty = JS_NewString(ctx, "");
		JSValueConst jarg = empty;
		out = JS_Call(ctx, join, parts, 1, &jarg);
		JS_FreeValue(ctx, empty);
		JS_FreeValue(ctx, join);
	}
done:
	JS_FreeValue(ctx, parts);
	JS_FreeValue(ctx, from);
	JS_FreeValue(ctx, strc);
	JS_FreeValue(ctx, global);
	return out;
}

static JSValue JsAtob(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	const char* s;
	unsigned char* out;
	size_t cap, o = 0;
	int val = 0;
	int bits = -8;
	(void)this_val;
	if (argc < 1)
		return JS_ThrowTypeError(ctx, "atob");
	s = JS_ToCString(ctx, argv[0]);
	if (!s)
		return JS_EXCEPTION;
	cap = strlen(s) + 1;
	out = (unsigned char*)malloc(cap ? cap : 1);
	if (!out) {
		JS_FreeCString(ctx, s);
		return JS_ThrowOutOfMemory(ctx);
	}
	for (size_t i = 0; s[i]; i++) {
		unsigned char c = (unsigned char)s[i];
		int d;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')
			continue;
		if (c == '=')
			break;
		d = NsB64Val(c);
		if (d < 0) {
			free(out);
			JS_FreeCString(ctx, s);
			return JS_ThrowTypeError(ctx, "atob");
		}
		val = (val << 6) + d;
		bits += 6;
		if (bits >= 0) {
			if (o + 1 >= cap) {
				free(out);
				JS_FreeCString(ctx, s);
				return JS_ThrowTypeError(ctx, "atob");
			}
			out[o++] = (unsigned char)((val >> bits) & 0xff);
			bits -= 8;
		}
	}
	{
		JSValue r = NsBytesToString(ctx, out, o);
		free(out);
		JS_FreeCString(ctx, s);
		return r;
	}
}

static const char kBtoaJs[] =
	"function btoa(s){s=String(s);"
	"var t='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';"
	"var parts=[],chunk='',i=0;"
	"while(i<s.length){"
	"var a=s.charCodeAt(i++),b=i<s.length?s.charCodeAt(i++):-1,c=i<s.length?s.charCodeAt(i++):-1;"
	"if(a>255||b>255||c>255)throw new TypeError('btoa');"
	"chunk+=t[a>>2]+t[((a&3)<<4)|(b<0?0:(b>>4))]+(b<0?'=':t[((b&15)<<2)|(c<0?0:(c>>6))])+(c<0?'=':t[c&63]);"
	"if(chunk.length>=1024){parts.push(chunk);chunk='';}"
	"}"
	"if(chunk)parts.push(chunk);return parts.join('');}";

/* Same string main.cpp sends as the HTTP User-Agent. The search
 * challenge reads this and the request header has to agree. */
static const char kJsUa[] =
	"Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

/* scheme://host[:port]/path split for the location snapshot. */
static void NsParseUrlParts(const char* url, char* host, size_t hostsz, char* path, size_t pathsz) {
	if (host && hostsz)
		host[0] = 0;
	if (path && pathsz)
		path[0] = 0;
	if (!url)
		return;
	const char* scp = strstr(url, "://");
	scp = scp ? scp + 3 : (char*)url;
	const char* slash = strchr(scp, '/');
	if (slash) {
		size_t hl = (size_t)(slash - scp);
		if (host && hl < hostsz) {
			memcpy(host, (void*)scp, hl);
			host[hl] = 0;
		}
		if (path)
			snprintf(path, pathsz, "%s", slash);
	} else if (host) {
		snprintf(host, hostsz, "%s", scp);
	}
}

static void NsRegisterHostApi(const char* url) {
	JSValue glob = JS_GetGlobalObject(s_ctx);

	/* window alias (the global object itself). */
	JS_SetPropertyStr(s_ctx, glob, "window", JS_DupValue(s_ctx, glob));

	JSValue cons = JS_NewObject(s_ctx);
	JS_SetPropertyStr(s_ctx, cons, "log", JS_NewCFunction(s_ctx, JsConsoleLog, "log", 1));
	JS_SetPropertyStr(s_ctx, cons, "info", JS_NewCFunction(s_ctx, JsConsoleLog, "info", 1));
	JS_SetPropertyStr(s_ctx, cons, "debug", JS_NewCFunction(s_ctx, JsConsoleLog, "debug", 1));
	JS_SetPropertyStr(s_ctx, cons, "warn", JS_NewCFunction(s_ctx, JsConsoleWarn, "warn", 1));
	JS_SetPropertyStr(s_ctx, cons, "error", JS_NewCFunction(s_ctx, JsConsoleError, "error", 1));
	JS_SetPropertyStr(s_ctx, glob, "console", cons);

	JSValue doc = JsSoftNew(s_ctx);
	JSValue body = JsMakeEl(s_ctx);
	JSValue head = JsMakeEl(s_ctx);
	JS_SetPropertyStr(s_ctx, doc, "write", JS_NewCFunction(s_ctx, JsDocumentWrite, "write", 1));
	JS_SetPropertyStr(s_ctx, doc, "writeln", JS_NewCFunction(s_ctx, JsDocumentWriteln, "writeln", 1));
	JS_SetPropertyStr(s_ctx, doc, "title", JS_NewString(s_ctx, s_title));
	JS_SetPropertyStr(s_ctx, doc, "readyState", JS_NewString(s_ctx, "complete"));
	JS_SetPropertyStr(s_ctx, doc, "hidden", JS_FALSE);
	JS_SetPropertyStr(s_ctx, doc, "URL", JS_NewString(s_ctx, url ? url : ""));
	{
		JSAtom atom = JS_NewAtom(s_ctx, "cookie");
		JSValue get = JS_NewCFunction(s_ctx, JsCookieGet, "get cookie", 0);
		JSValue set = JS_NewCFunction(s_ctx, JsCookieSet, "set cookie", 1);
		JS_DefinePropertyGetSet(s_ctx, doc, atom, get, set, JS_PROP_CONFIGURABLE);
		JS_FreeAtom(s_ctx, atom);
	}
	JS_SetPropertyStr(s_ctx, doc, "body", body);
	JS_SetPropertyStr(s_ctx, doc, "head", head);
	JS_SetPropertyStr(s_ctx, doc, "documentElement", JS_DupValue(s_ctx, body));
	JS_SetPropertyStr(s_ctx, doc, "addEventListener",
					  JS_NewCFunction(s_ctx, JsNop, "addEventListener", 2));
	JS_SetPropertyStr(s_ctx, doc, "removeEventListener",
					  JS_NewCFunction(s_ctx, JsNop, "removeEventListener", 2));
	JS_SetPropertyStr(s_ctx, doc, "getElementById",
					  JS_NewCFunction(s_ctx, JsGetElById, "getElementById", 1));
	JS_SetPropertyStr(s_ctx, doc, "querySelector",
					  JS_NewCFunction(s_ctx, JsGetEl, "querySelector", 1));
	JS_SetPropertyStr(s_ctx, doc, "querySelectorAll",
					  JS_NewCFunction(s_ctx, JsQueryAll, "querySelectorAll", 1));
	JS_SetPropertyStr(s_ctx, doc, "getElementsByTagName",
					  JS_NewCFunction(s_ctx, JsQueryAll, "getElementsByTagName", 1));
	JS_SetPropertyStr(s_ctx, doc, "getElementsByClassName",
					  JS_NewCFunction(s_ctx, JsQueryAll, "getElementsByClassName", 1));
	JS_SetPropertyStr(s_ctx, doc, "createElement",
					  JS_NewCFunction(s_ctx, JsGetEl, "createElement", 1));
	JS_SetPropertyStr(s_ctx, doc, "createElementNS",
					  JS_NewCFunction(s_ctx, JsGetEl, "createElementNS", 2));
	JS_SetPropertyStr(s_ctx, glob, "document", doc);
	JS_SetPropertyStr(s_ctx, glob, "addEventListener",
					  JS_NewCFunction(s_ctx, JsNop, "addEventListener", 2));
	JS_SetPropertyStr(s_ctx, glob, "removeEventListener",
					  JS_NewCFunction(s_ctx, JsNop, "removeEventListener", 2));

	char host[256], path[1536];
	const char* u = url ? url : "";
	NsParseUrlParts(u, host, sizeof(host), path, sizeof(path));
	bool tls = !strncasecmp(u, "https://", 8);
	const char* scheme = tls ? "https" : "http";
	const char* q = strchr(u, '?');
	const char* h = strchr(u, '#');
	char search[1536] = "", hash[256] = "", pathname[1536] = "", origin[320];
	if (h)
		snprintf(hash, sizeof(hash), "%s", h);
	if (q && (!h || q < h)) {
		size_t n = h ? (size_t)(h - q) : strlen(q);
		if (n >= sizeof(search))
			n = sizeof(search) - 1;
		memcpy(search, (void*)q, n);
		search[n] = 0;
	}
	{
		size_t n = 0;
		while (path[n] && path[n] != '?' && path[n] != '#')
			n++;
		if (n >= sizeof(pathname))
			n = sizeof(pathname) - 1;
		memcpy(pathname, path, n);
		pathname[n] = 0;
	}
	char hostname[256];
	const char* colon = strrchr(host, ':');
	size_t hn = colon ? (size_t)(colon - host) : strlen(host);
	if (hn >= sizeof(hostname))
		hn = sizeof(hostname) - 1;
	memcpy(hostname, host, hn);
	hostname[hn] = 0;
	snprintf(origin, sizeof(origin), "%s://%s", scheme, host);

	JSValue loc = JS_NewObject(s_ctx);
	{
		JSAtom atom = JS_NewAtom(s_ctx, "href");
		JSValue get = JS_NewCFunction(s_ctx, JsLocHrefGet, "get href", 0);
		JSValue set = JS_NewCFunction(s_ctx, JsLocGo, "set href", 1);
		JS_DefinePropertyGetSet(s_ctx, loc, atom, get, set, JS_PROP_CONFIGURABLE);
		JS_FreeAtom(s_ctx, atom);
	}
	JS_SetPropertyStr(s_ctx, loc, "host", JS_NewString(s_ctx, host));
	JS_SetPropertyStr(s_ctx, loc, "hostname", JS_NewString(s_ctx, hostname));
	JS_SetPropertyStr(s_ctx, loc, "path", JS_NewString(s_ctx, path));
	JS_SetPropertyStr(s_ctx, loc, "pathname", JS_NewString(s_ctx, pathname));
	JS_SetPropertyStr(s_ctx, loc, "search", JS_NewString(s_ctx, search));
	JS_SetPropertyStr(s_ctx, loc, "hash", JS_NewString(s_ctx, hash));
	JS_SetPropertyStr(s_ctx, loc, "origin", JS_NewString(s_ctx, origin));
	JS_SetPropertyStr(s_ctx, loc, "port", JS_NewString(s_ctx, colon ? colon + 1 : ""));
	JS_SetPropertyStr(s_ctx, loc, "protocol", JS_NewString(s_ctx, tls ? "https:" : "http:"));
	JS_SetPropertyStr(s_ctx, loc, "replace", JS_NewCFunction(s_ctx, JsLocGo, "replace", 1));
	JS_SetPropertyStr(s_ctx, loc, "assign", JS_NewCFunction(s_ctx, JsLocGo, "assign", 1));
	JS_SetPropertyStr(s_ctx, loc, "reload", JS_NewCFunction(s_ctx, JsLocGo, "reload", 0));
	JS_SetPropertyStr(s_ctx, loc, "toString", JS_NewCFunction(s_ctx, JsLocToString, "toString", 0));
	JS_SetPropertyStr(s_ctx, glob, "location", loc);
	JS_SetPropertyStr(s_ctx, doc, "location", JS_DupValue(s_ctx, loc));

	JSValue nav = JS_NewObject(s_ctx);
	JS_SetPropertyStr(s_ctx, nav, "userAgent", JS_NewString(s_ctx, kJsUa));
	JS_SetPropertyStr(s_ctx, nav, "language", JS_NewString(s_ctx, "en-US"));
	JS_SetPropertyStr(s_ctx, nav, "platform", JS_NewString(s_ctx, "Linux aarch64"));
	JS_SetPropertyStr(s_ctx, nav, "vendor", JS_NewString(s_ctx, "Google Inc."));
	JS_SetPropertyStr(s_ctx, nav, "webdriver", JS_FALSE);
	JS_SetPropertyStr(s_ctx, nav, "cookieEnabled", JS_TRUE);
	/* The search wall beacons and then continues. A blocking POST here
	 * would stall the timer that unhides the page. */
	JS_SetPropertyStr(s_ctx, nav, "sendBeacon",
					  JS_NewCFunction(s_ctx, JsRetTrue, "sendBeacon", 2));
	JS_SetPropertyStr(s_ctx, glob, "navigator", nav);

	JSValue perf = JS_NewObject(s_ctx);
	JSValue timing = JS_NewObject(s_ctx);
	JS_SetPropertyStr(s_ctx, timing, "navigationStart", JS_NewFloat64(s_ctx, 1));
	JS_SetPropertyStr(s_ctx, timing, "responseStart", JS_NewFloat64(s_ctx, 5));
	JS_SetPropertyStr(s_ctx, perf, "timing", timing);
	JS_SetPropertyStr(s_ctx, perf, "timeOrigin", JS_NewFloat64(s_ctx, 0));
	JS_SetPropertyStr(s_ctx, perf, "now", JS_NewCFunction(s_ctx, JsPerfNow, "now", 0));
	JS_SetPropertyStr(s_ctx, perf, "mark", JS_NewCFunction(s_ctx, JsNop, "mark", 1));
	JS_SetPropertyStr(s_ctx, perf, "measure", JS_NewCFunction(s_ctx, JsNop, "measure", 1));
	JS_SetPropertyStr(s_ctx, perf, "getEntries",
					  JS_NewCFunction(s_ctx, JsPerfEntries, "getEntries", 0));
	JS_SetPropertyStr(s_ctx, perf, "getEntriesByType",
					  JS_NewCFunction(s_ctx, JsPerfByType, "getEntriesByType", 1));
	JS_SetPropertyStr(s_ctx, perf, "getEntriesByName",
					  JS_NewCFunction(s_ctx, JsPerfEntries, "getEntriesByName", 1));
	JS_SetPropertyStr(s_ctx, glob, "performance", perf);
	JSValue imageCtor = JS_NewCFunction2(s_ctx, JsImageCtor, "Image", 2, JS_CFUNC_constructor, 0);
	JSValue imageProto = JS_NewObject(s_ctx);
	JS_SetConstructor(s_ctx, imageCtor, imageProto);
	JS_SetPropertyStr(s_ctx, glob, "Image", imageCtor);
	JS_FreeValue(s_ctx, imageProto);

	JS_SetPropertyStr(s_ctx, glob, "atob", JS_NewCFunction(s_ctx, JsAtob, "atob", 1));
	{
		JSValue ret = JS_Eval(s_ctx, kBtoaJs, strlen(kBtoaJs), "<btoa>", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(ret))
			NsReportException("btoa");
		else
			JS_FreeValue(s_ctx, ret);
	}
	/* Absent on purpose. A soft prototype would make these truthy, and the
	 * search script would then call a stub instead of setTimeout. */
	JS_SetPropertyStr(s_ctx, glob, "requestIdleCallback", JS_UNDEFINED);
	JS_SetPropertyStr(s_ctx, glob, "setImmediate", JS_UNDEFINED);
	JS_SetPropertyStr(s_ctx, glob, "trustedTypes", JS_UNDEFINED);
	JS_SetPropertyStr(s_ctx, glob, "self", JS_DupValue(s_ctx, glob));
	JS_SetPropertyStr(s_ctx, glob, "top", JS_DupValue(s_ctx, glob));
	JS_SetPropertyStr(s_ctx, glob, "parent", JS_DupValue(s_ctx, glob));
	JS_SetPropertyStr(s_ctx, glob, "setTimeout", JS_NewCFunction(s_ctx, JsTimeoutFn, "setTimeout", 2));
	JS_SetPropertyStr(s_ctx, glob, "setInterval", JS_NewCFunction(s_ctx, JsIntervalFn, "setInterval", 2));
	JS_SetPropertyStr(s_ctx, glob, "clearTimeout",
					  JS_NewCFunction(s_ctx, JsClearTimer, "clearTimeout", 1));
	JS_SetPropertyStr(s_ctx, glob, "clearInterval",
					  JS_NewCFunction(s_ctx, JsClearTimer, "clearInterval", 1));
	JS_SetPropertyStr(s_ctx, glob, "requestAnimationFrame",
					  JS_NewCFunction(s_ctx, JsRequestAnimationFrame, "requestAnimationFrame", 1));

	JS_SetPropertyStr(s_ctx, glob, "alert", JS_NewCFunction(s_ctx, JsAlert, "alert", 1));

	{
		JSValue store = JsSoftNew(s_ctx);
		JS_SetPropertyStr(s_ctx, store, "getItem", JS_NewCFunction(s_ctx, JsRetEmpty, "getItem", 1));
		JS_SetPropertyStr(s_ctx, store, "setItem", JS_NewCFunction(s_ctx, JsNop, "setItem", 2));
		JS_SetPropertyStr(s_ctx, store, "removeItem",
						  JS_NewCFunction(s_ctx, JsNop, "removeItem", 1));
		JS_SetPropertyStr(s_ctx, store, "length", JS_NewInt32(s_ctx, 0));
		JS_SetPropertyStr(s_ctx, glob, "localStorage", store);
		JS_SetPropertyStr(s_ctx, glob, "sessionStorage", JS_DupValue(s_ctx, store));
	}
	/* Missing window.foo stays undefined, same as a browser. A truthy
	 * stand-in makes the search wall skip its own check. */
	{
		JSValue gproto = JS_GetPrototype(s_ctx, glob);
		if (JS_IsNull(gproto) || JS_IsUndefined(gproto)) {
			JSValue std;
			/* glob may have a null prototype; make sure the injected
			 * Soft mid object still inherits Object.prototype. */
			JS_FreeValue(s_ctx, gproto);
			std = JS_NewObject(s_ctx);
			if (JS_IsException(std)) {
				JS_FreeValue(s_ctx, glob);
				return;
			}
			gproto = JS_GetPrototype(s_ctx, std);
			JS_FreeValue(s_ctx, std);
		}
		if (!JS_IsException(gproto)) {
			JSValue mid = JsSoftNew(s_ctx);
			if (!JS_IsException(mid)) {
				JS_SetPrototype(s_ctx, mid, gproto);
				JS_SetPrototype(s_ctx, glob, mid);
				JS_FreeValue(s_ctx, mid);
			}
			JS_FreeValue(s_ctx, gproto);
		}
	}

	JS_FreeValue(s_ctx, glob);
}

static void NsJsDestroy(void) {
	for (int i = 0; i < NS_JS_TIMER_MAX; i++) {
		if (s_timers[i].used) {
			s_timers[i].used = false;
			/* Drop the callback ref while the context is still alive:
			 * JS_FreeRuntime asserts if a page leaks one at teardown. */
			if (s_ctx)
				JS_FreeValue(s_ctx, s_timers[i].cb);
			s_timers[i].cb = JS_UNDEFINED;
		}
	}
	if (s_ctx)
		JS_FreeContext(s_ctx);
	s_ctx = NULL;
	if (s_rt)
		JS_FreeRuntime(s_rt);
	s_rt = NULL;
	s_navPending = 0;
	s_nav[0] = 0;
	s_pumpCapped = 0;
	s_softReady = 0;
	s_softClass = 0;
	s_elClass = 0;
	s_jsRoot = NULL;
	s_domDirty = 0;
	s_scriptLen = 0;
	s_scriptExternal = false;
	s_scriptNotJs = false;
	s_srcAttr[0] = 0;
	s_conHead = s_conTail = 0;
}

void NsJsSetDocument(NsCssNode* root) {
	s_jsRoot = root;
	s_domDirty = 0;
}

int NsJsConsumeDirty(void) {
	int d = s_domDirty;
	s_domDirty = 0;
	return d;
}

/* bing.com and any subdomain. The static page is what we paint. */
static int NsJsHostIsBing(const char* url) {
	const char* p;
	const char* host;
	const char* end;
	size_t n;
	const char root[] = "bing.com";
	const size_t rn = sizeof(root) - 1;
	if (!url || !url[0])
		return 0;
	p = strstr(url, "://");
	host = p ? p + 3 : url;
	if (host[0] == '[')
		return 0;
	end = host;
	while (*end && *end != '/' && *end != '?' && *end != '#' && *end != ':')
		end++;
	n = (size_t)(end - host);
	if (n == rn && strncasecmp(host, root, rn) == 0)
		return 1;
	if (n > rn + 1 && host[n - rn - 1] == '.' && strncasecmp(host + (n - rn), root, rn) == 0)
		return 1;
	return 0;
}

int NsJsActive(void) {
	return s_ctx != NULL;
}

void NsJsBegin(const char* url) {
	NsJsDestroy();
	if (!s_jsEnabled)
		return;
	if (url)
		snprintf(s_url, sizeof(s_url), "%s", url);
	else
		s_url[0] = 0;
	s_title[0] = 0;
	if (NsJsHostIsBing(s_url)) {
		_KePrint("[js] off bing\n");
		return;
	}
	s_navMs = _KeGetCurrentMS();

	s_rt = JS_NewRuntime();
	if (!s_rt)
		return;
	JS_SetMemoryLimit(s_rt, 8 * 1024 * 1024);
	JS_SetMaxStackSize(s_rt, 256 * 1024);
	s_ctx = JS_NewContext(s_rt);
	if (!s_ctx) {
		JS_FreeRuntime(s_rt);
		s_rt = NULL;
		return;
	}
	NsRegisterHostApi(s_url);
}

void NsJsEnd(void) {
	NsJsDestroy();
}

/* Preload document.title from the page's first <title>...</title>. */
void NsJsScanTitle(const char* src, size_t len) {
	if (!src)
		return;
	for (size_t i = 0; i + 6 < len; i++) {
		if (src[i] == '<' && !strncasecmp(src + i + 1, "title", 5)) {
			size_t j = i + 6;
			while (j < len && src[j] != '>')
				j++;
			j++;
			size_t tl = 0;
			while (j < len && tl + 1 < sizeof(s_title)) {
				if (src[j] == '<' && j + 8 <= len && !strncasecmp(src + j + 1, "/title", 6))
					break;
				s_title[tl++] = src[j++];
			}
			s_title[tl] = 0;
			/* trim edges */
			char* s = s_title;
			while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
				s++;
			memmove(s_title, s, strlen(s) + 1);
			size_t e = strlen(s_title);
			while (e > 0 && (s_title[e - 1] == ' ' || s_title[e - 1] == '\t' ||
							 s_title[e - 1] == '\n' || s_title[e - 1] == '\r'))
				s_title[--e] = 0;
			return;
		}
	}
}

void NsJsScriptSrcAttr(const char* attr) {
	s_srcAttr[0] = 0;
	s_scriptExternal = false;
	if (attr && attr[0]) {
		snprintf(s_srcAttr, sizeof(s_srcAttr), "%s", attr);
		s_scriptExternal = true;
	}
}

static int NsJsIsJsType(const char* type) {
	if (!type || !type[0])
		return 1;
	if (strcasestr(type, "javascript") || strcasestr(type, "ecmascript"))
		return 1;
	return 0;
}

void NsJsScriptType(const char* type) {
	s_scriptNotJs = !NsJsIsJsType(type);
}

void NsJsScriptChunk(char c) {
	if (!s_jsEnabled)
		return;
	if (s_scriptLen < NS_JS_SCRIPT_MAX)
		s_script[s_scriptLen++] = c;
	s_script[s_scriptLen] = 0;
}

static void NsReportException(const char* where) {
	JSValue ex = JS_GetException(s_ctx);
	JSValue namev = JS_IsObject(ex) ? JS_GetPropertyStr(s_ctx, ex, "name") : JS_UNDEFINED;
	const char* name = JS_IsUndefined(namev) ? NULL : JS_ToCString(s_ctx, namev);
	const char* msg = JS_ToCString(s_ctx, ex);
	const char* shown = "?";
	char buf[NS_JS_CONSOLE_LINE];
	int nlen = 0;
	int safe = name && name[0];
	size_t i;
	/* The message can carry a challenge string. Serial gets the name
	 * and the length only. */
	if (safe) {
		for (i = 0; name[i]; i++) {
			char c = name[i];
			if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) || i > 24) {
				safe = 0;
				break;
			}
		}
	}
	if (safe)
		shown = name;
	if (msg)
		nlen = (int)strlen(msg);
	_KePrint("[js] %s %s %d\n", where ? where : "?", shown, nlen);
	snprintf(buf, sizeof(buf), "%s%s%s", where ? where : "", where ? ": " : "",
			 (msg && msg[0]) ? msg : "<exception>");
	NsConsolePush(NS_JS_KIND_ERR, buf, strlen(buf));
	if (name)
		JS_FreeCString(s_ctx, name);
	JS_FreeValue(s_ctx, namev);
	if (msg)
		JS_FreeCString(s_ctx, msg);
	JS_FreeValue(s_ctx, ex);
}

int NsJsDefined(const char* name) {
	JSValue g;
	JSValue v;
	int ok;
	if (!s_ctx || !name || !name[0])
		return 0;
	g = JS_GetGlobalObject(s_ctx);
	v = JS_GetPropertyStr(s_ctx, g, name);
	ok = JS_IsObject(v);
	JS_FreeValue(s_ctx, v);
	JS_FreeValue(s_ctx, g);
	return ok;
}

void NsJsRunPending(void) {
	if (!s_jsEnabled || !s_ctx) {
		s_scriptLen = 0;
		s_scriptExternal = false;
		return;
	}
	if (s_scriptExternal || s_scriptNotJs) {
		/* External src is not fetched. ld+json is not a script. */
	} else if (s_scriptLen > 0) {
		s_script[s_scriptLen] = 0;
		JSValue ret = JS_Eval(s_ctx, s_script, s_scriptLen, "<script>", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(ret)) {
			NsReportException("script");
		} else {
			JS_FreeValue(s_ctx, ret);
		}
		NsJsPump(256);
	}
	NsJsFlushConsole();
	s_scriptLen = 0;
	s_scriptExternal = false;
	s_scriptNotJs = false;
	s_srcAttr[0] = 0;
}
