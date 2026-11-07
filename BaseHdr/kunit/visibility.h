/*
 * DCL kunit/visibility.h -- __visible_for_testing() is defined empty.
 *
 * vkms and drm_gem_shmem_helper attach the annotation to declarations; no
 * call site depends on it having an effect (the kunit test objects that
 * would reach across the visibility boundary are not built here).
 */
#ifndef _KUNIT_VISIBILITY_H
#define _KUNIT_VISIBILITY_H

#define __visible_for_testing

#endif /* _KUNIT_VISIBILITY_H */
