#ifndef __DEV_CLIP_H__
#define __DEV_CLIP_H__

#include <stdint.h>
#include <aurora.h>

struct __VFS_NODE__;
typedef struct __VFS_NODE__ AuVFSNode;

/* One buffer, whole-clipboard semantics. A write replaces the contents, a read
 * reports what is there without consuming it -- pasting twice has to paste
 * twice -- and CLIP_IOCODE_CLEAR empties it. Sized for a command line or a
 * block of text rather than for a file; pasting a file belongs in the VFS. */
#define CLIPBOARD_MAX	4096

#define CLIP_IOCODE_CLEAR 1

/* AuDevClipInitialise -- mounts /dev/clipboard on the device file system */
extern void AuDevClipInitialise();

/* AuClipboardSet -- replace the clipboard; a NULL source empties it */
extern void AuClipboardSet(const void* src, uint32_t len);

/* AuClipboardGet -- copy out up to max bytes, leaving the clipboard intact */
extern uint32_t AuClipboardGet(void* dst, uint32_t max);

/* AuClipboardLength -- bytes currently held, 0 when empty */
extern uint32_t AuClipboardLength();

/* AuClipboardClear -- empty the clipboard */
extern void AuClipboardClear();

#endif
