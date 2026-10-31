/*
 * DCL hrtimer_types.h -- DCL's linux/hrtimer.h already defines
 * struct hrtimer (drm_vblank_helper.h includes this name first), so
 * redirect rather than define the struct twice.
 */
#ifndef _LINUX_HRTIMER_TYPES_H
#define _LINUX_HRTIMER_TYPES_H

#include <linux/hrtimer.h>

#endif /* _LINUX_HRTIMER_TYPES_H */
