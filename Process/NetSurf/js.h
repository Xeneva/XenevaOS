/**
 * NetSurf QuickJS host for XenevaOS.
 *
 * Runs page <script> bodies in document order while the readability pass
 * parses. No DOM tree yet: the host exposes a scripting shell that can run
 * logic and emit text through the renderer -- console lines and
 * document.write output land inline at the position the script executes.
 *
 * Exposes only QuickJS's core intrinsics (Math/JSON/RegExp/Intl...); the
 * DOM, CSSOM, images and network fetchers are future work (README).
 */

#ifndef NS_JS_H
#define NS_JS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NS_JS_SCRIPT_MAX (64 * 1024)

/* Sink kinds: how a JS output chunk must render. */
#define NS_JS_KIND_WRITE 0 /* document.write: carries the current run style */
#define NS_JS_KIND_LOG	 1 /* console.log/info/debug: dim gray */
#define NS_JS_KIND_WARN	 2 /* console.warn + alert: amber */
#define NS_JS_KIND_ERR	 3 /* console.error + uncaught exceptions: red */

typedef void NsJsSinkFn(const char* s, size_t n, int kind);

/* One sink for the whole page; set before the first fetch. */
void NsJsSetSink(NsJsSinkFn* fn);

/* Per-page lifecycle: NsJsBegin replaces any previous context (dropping
 * its timers), scans <title>, then NsJsScriptChunk accumulates each
 * <script> body until </script> runs NsJsRunPending. The context (and any
 * armed timers) survives past the parse until the next NsJsBegin. */
void NsJsBegin(const char* url);
void NsJsScanTitle(const char* src, size_t len);
void NsJsScriptChunk(char c);
void NsJsScriptSrcAttr(const char* attr); /* len>0 => external, not fetched */
void NsJsScriptType(const char* type);	 /* non-JS types (ld+json) are not evaled */
void NsJsRunPending(void);				 /* </script>: eval accumulated body */
void NsJsFlushConsole(void);			 /* drain console lines to the sink */
void NsJsEnd(void);						 /* destroy context (rarely needed) */

/* Fire due timers and promise jobs. Returns true if a callback ran
 * (caller repaints). Poll every few ms when the postbox is idle. */
bool NsJsTick(void);
int NsJsTimerCount(void); /* armed timers, for loop heuristics */

/* 1 if a script asked to load another page. Copy the URL out and clear
 * the request. The caller fetches it after the script returns. */
int NsJsConsumeNav(char* out, size_t outsz);

/* The DOM exists only after the hubbub parse. Scripts that run during
 * the HTML scan still see a soft document; timers that fire later see
 * this tree. A setAttribute that changes style sets the dirty flag. */
struct NsCssNode;
void NsJsSetDocument(struct NsCssNode* root);
int NsJsConsumeDirty(void);

const char* NsJsVersion(void); /* "QuickJS 2026-06-04" */

/* 0 when this page did not start QuickJS. Bing is one of those pages. */
int NsJsActive(void);

/* 1 when the page assigned this global to an object. */
int NsJsDefined(const char* name);

#ifdef __cplusplus
}
#endif

#endif
