/*
 * DCL mm_types.h -- the folio shape drm's GEM paths need, nothing else.
 *
 * DCL has no page cache; struct page lives in pagemap.h for the tty side.
 * drm_gem.c/drm_gem_shmem_helper.c batch and mark folios of shmem-backed
 * GEM imports, which vkms never exercises at runtime today, so the helpers
 * answer honestly-but-blandly: an order-0 folio of one page that is never
 * dirty, never pmd-mappable, and needs no writeback.  page_folio() is a
 * cast because DCL pages are not compound.
 */
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/types.h>

struct folio {
	unsigned long flags;
};

struct page;

static inline struct folio *page_folio(const struct page *p)
{
	return (struct folio *)(void *)p;
}

static inline struct page *folio_file_page(struct folio *folio, unsigned long idx)
{
	(void)idx;
	return (struct page *)(void *)folio;
}

static inline unsigned int folio_nr_pages(const struct folio *folio)
{
	(void)folio;
	return 1;
}

static inline unsigned long folio_pfn(const struct folio *folio)
{
	(void)folio;
	return 0;
}

static inline bool folio_test_pmd_mappable(const struct folio *folio)
{
	(void)folio;
	return false;
}

static inline int folio_mark_dirty(struct folio *folio)
{
	(void)folio;
	return 0;
}

static inline void folio_mark_accessed(struct folio *folio)
{
	(void)folio;
}

static inline void folio_put(struct folio *folio)
{
	(void)folio;
}

#endif /* _LINUX_MM_TYPES_H */
