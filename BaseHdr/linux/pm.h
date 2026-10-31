#ifndef __LINUX_PM_H__
#define __LINUX_PM_H__

/*
 * DCL <linux/pm.h> -- pm_sleep_ptr(), and nothing else.
 *
 * mainline's header is ~37 KB of suspend/resume types, dpm_list, PM message
 * bits and the dev_pm_info layout. DCL has no suspend path: there is no
 * freezer, no dpm_suspend(), nothing that walks a driver's ->freeze. So the
 * whole surface that a ported source actually names is one macro, and
 * pretending the rest exists would be worse than it being absent.
 *
 * The macro's job upstream:
 *
 *     #define pm_sleep_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM_SLEEP), (_ptr))
 *
 * -- i.e. the driver's .freeze/.restore callbacks compile away to NULL when
 * the kernel was built without suspend support, which is why virtio-rng.c can
 * name virtrng_freeze and virtrng_restore unconditionally.
 *
 * ### DCL spells the same result differently, and the spelling matters
 *
 * The obvious translations both fail:
 *
 *   - `#define pm_sleep_ptr(_ptr) (_ptr)` keeps the pointer, so .freeze/.restore
 *     get wired to functions nothing calls -- correct by accident, but it is
 *     not what the macro means.
 *   - `#define pm_sleep_ptr(_ptr) NULL` is the right *value* but drops the
 *     token `_ptr` entirely, so virtrng_freeze and virtrng_restore become two
 *     static functions with no reference anywhere: -Wunused-function on every
 *     build. It also cannot be typed -- assigning `((void*)0)` to a function
 *     pointer only compiles because we do not pass -pedantic.
 *
 * `((__typeof__(&_ptr))0)` does all three at once: it references `_ptr` (the
 * function counts as used, no warning), the cast yields a null pointer of
 * exactly the callee's function-pointer type (no type-mismatch warning, no
 * reliance on -Wno-pedantic), and `cast-of-0-to-pointer` is a null pointer
 * constant, so it is a legal constant expression for the static initialiser
 * it appears in. A comma-operator spelling, e.g. `((void)sizeof(&(_ptr)),
 * NULL)`, would be rejected: initialisers at file scope need a constant
 * expression and the comma operator is not one.
 */

#define pm_sleep_ptr(_ptr) (_ptr)

/*
 * struct dev_pm_ops + SET_SYSTEM_SLEEP_PM_OPS() -- the PCI driver's power
 * half that bochs.c brings in:
 *
 *     static const struct dev_pm_ops bochs_pm_ops = {
 *         SET_SYSTEM_SLEEP_PM_OPS(bochs_pm_suspend, bochs_pm_resume)
 *     };
 *     ...
 *     static struct pci_driver bochs_pci_driver = {
 *         ...
 *         .driver.pm = &bochs_pm_ops,
 *     };
 *
 * The callbacks themselves sit behind `#ifdef CONFIG_PM_SLEEP` in the
 * driver, and CONFIG_PM_SLEEP is undefined here for the reason pm_sleep_ptr
 * above records -- there is no suspend path. So mainline's #else arm is the
 * one that has to exist: an empty expansion that names neither function,
 * because both were compiled out. A non-empty expansion would reference two
 * functions that do not exist.
 *
 * The struct is correspondingly empty: a zero-member dev_pm_ops is exactly
 * what a driver with no compiled-in PM callbacks points at. If a suspend
 * path ever lands, the members arrive with it and this macro grows mainline's
 * populated arm -- nothing else in the tree names either today.
 */
struct dev_pm_ops {
	int (*prepare)(struct device*);
	void (*complete)(struct device*);
	int (*suspend)(struct device*);
	int (*resume)(struct device*);
	int (*freeze)(struct device*);
	int (*thaw)(struct device*);
	int (*poweroff)(struct device*);
	int (*restore)(struct device*);
	int (*suspend_late)(struct device*);
	int (*resume_early)(struct device*);
	int (*freeze_late)(struct device*);
	int (*thaw_early)(struct device*);
	int (*poweroff_late)(struct device*);
	int (*restore_early)(struct device*);
	int (*suspend_noirq)(struct device*);
	int (*resume_noirq)(struct device*);
	int (*freeze_noirq)(struct device*);
	int (*thaw_noirq)(struct device*);
	int (*poweroff_noirq)(struct device*);
	int (*restore_noirq)(struct device*);
	int (*runtime_suspend)(struct device*);
	int (*runtime_resume)(struct device*);
	int (*runtime_idle)(struct device*);
};

#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)

#endif /* __LINUX_PM_H__ */
