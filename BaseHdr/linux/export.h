#ifndef __LINUX_EXPORT_H__
#define __LINUX_EXPORT_H__

/*
 * DCL <linux/export.h> -- EXPORT_SYMBOL family. DCL compiles ports straight
 * into the kernel image rather than into modules, so exporting is a no-op
 * here; a port built as a .ko resolves symbols through
 * KernelAA64/kernel_exports.c instead. No-ops keep both worlds' source
 * compiling unchanged.
 */

#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define EXPORT_SYMBOL_NS(sym, ns)
#define EXPORT_SYMBOL_NS_GPL(sym, ns)

/*
 * EXPORT_SYMBOL_IF_KUNIT(sym) -- "export this only in a KUnit build", so the
 * symbol exists for tests and not for a shippable image. mainline guards the
 * two spellings on CONFIG_KUNIT: EXPORT_SYMBOL(sym) when it is set, the empty
 * macro otherwise. Both arms are this macro here, and not as a simplification
 * -- CONFIG_KUNIT has no referent, and EXPORT_SYMBOL is already the empty
 * macro, so the guard would be choosing between two spellings of nothing.
 *
 * Four consumers, all drm: drm_color_mgmt.c:499/:515 export the maximum-
 * luminance property tests, vkms_composer.c and vkms_formats.c do the same
 * for their CTM and format helpers, vkms_config.c for the config accessors.
 * All four sit in the tree's *first* configuration-tunable code, which is
 * worth noting: the call sites are the point, and they are the first thing
 * here written against a test harness that does not exist yet.
 *
 * It is a macro at all rather than nothing, because an undefined
 * EXPORT_SYMBOL_IF_KUNIT(name) at file scope reads as a function *declaration*
 * -- "type specifier missing, defaults to int" plus "a parameter list without
 * types is only allowed in a function definition", one error at the site of
 * the export and none of them naming the macro.
 */
#define EXPORT_SYMBOL_IF_KUNIT(sym)

/*
 * VISIBLE_IF_KUNIT(sym) -- the *declaration* half of the same pair, and the
 * reason it is here at all is vkms_composer.c:67, :78, :86 and :132.
 *
 * Unlike every other macro in this header, that one prefixes a function *definition*
 * -- `VISIBLE_IF_KUNIT u16 lerp_u16(u16 a, u16 b, s64 t)` -- so an undefined
 * spelling does not decay into a harmless no-op the way an EXPORT_* line
 * would: it leaves a bare identifier where the return type belongs, and clang
 * reports "unknown type name 'VISIBLE_IF_KUNIT'" at each of the four, plus
 * "expected ';' after top level declarator" on the closing brace of each.
 *
 * Empty, and the emptiness is the honest answer rather than a shortcut. In
 * mainline the attribute carries the visibility that makes an exported symbol
 * survive a non-KUnit link; DCL drops the export (EXPORT_SYMBOL_IF_KUNIT
 * above), so there is nothing left to keep visible.
 *
 * Object-like, not function-like: the four sites write it with no argument
 * list at all -- `VISIBLE_IF_KUNIT u16 lerp_u16(u16 a, u16 b, s64 t)` -- so a
 * parameterised definition would never fire, since a function-like macro only
 * expands when the next token is `(`. A parameter here would be a definition
 * that never runs.
 */
#define VISIBLE_IF_KUNIT

#endif /* __LINUX_EXPORT_H__ */
