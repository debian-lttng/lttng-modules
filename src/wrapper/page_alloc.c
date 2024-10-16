/* SPDX-License-Identifier: (GPL-2.0-only or LGPL-2.1-only)
 * wrapper/page_alloc.c
 *
 * wrapper around get_pfnblock_flags_mask and Ubuntu
 * get_pageblock_flags_mask. Using KALLSYMS to get their address when
 * available, else we need to have a kernel that exports this function
 * to GPL modules.
 *
 * Copyright (C) 2015 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#include <lttng/kernel-version.h>

#if (defined(CONFIG_KALLSYMS) \
	&& (LTTNG_LINUX_VERSION_CODE >= LTTNG_KERNEL_VERSION(3,19,2)	\
		|| LTTNG_KERNEL_RANGE(3,14,36, 3,15,0)		\
		|| LTTNG_KERNEL_RANGE(3,18,10, 3,19,0)		\
		|| LTTNG_DEBIAN_KERNEL_RANGE(3,16,7,9,0,0, 3,17,0,0,0,0) \
		|| LTTNG_UBUNTU_KERNEL_RANGE(3,16,7,34, 3,17,0,0)))

/* Include page_alloc wrapper before pageblock-flags.h. */
#include <wrapper/page_alloc.h>

#include <linux/pageblock-flags.h>
#include <linux/kallsyms.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <wrapper/kallsyms.h>
#include <lttng/kernel-version.h>

#if (LTTNG_LINUX_VERSION_CODE >= LTTNG_KERNEL_VERSION(5,14,0))
static
unsigned long (*get_pfnblock_flags_mask_sym)(const struct page *page,
		unsigned long pfn,
		unsigned long mask);

unsigned long wrapper_get_pfnblock_flags_mask(const struct page *page,
		unsigned long pfn,
		unsigned long mask)
{
	WARN_ON_ONCE(!get_pfnblock_flags_mask_sym);
	if (get_pfnblock_flags_mask_sym) {
		struct irq_ibt_state irq_ibt_state;
		unsigned long ret;

		irq_ibt_state = wrapper_irq_ibt_save();
		ret = get_pfnblock_flags_mask_sym(page, pfn, mask);
		wrapper_irq_ibt_restore(irq_ibt_state);
		return ret;
	} else {
		return -ENOSYS;
	}
}
#elif (LTTNG_LINUX_VERSION_CODE >= LTTNG_KERNEL_VERSION(5,9,0))
static
unsigned long (*get_pfnblock_flags_mask_sym)(struct page *page,
		unsigned long pfn,
		unsigned long mask);

unsigned long wrapper_get_pfnblock_flags_mask(struct page *page,
		unsigned long pfn,
		unsigned long mask)
{
	WARN_ON_ONCE(!get_pfnblock_flags_mask_sym);
	if (get_pfnblock_flags_mask_sym) {
		struct irq_ibt_state irq_ibt_state;
		unsigned long ret;

		irq_ibt_state = wrapper_irq_ibt_save();
		ret = get_pfnblock_flags_mask_sym(page, pfn, mask);
		wrapper_irq_ibt_restore(irq_ibt_state);
		return ret;
	} else {
		return -ENOSYS;
	}
}
#else	/* #if (LTTNG_LINUX_VERSION_CODE >= LTTNG_KERNEL_VERSION(5,9,0)) */
static
unsigned long (*get_pfnblock_flags_mask_sym)(struct page *page,
		unsigned long pfn,
		unsigned long end_bitidx,
		unsigned long mask);

unsigned long wrapper_get_pfnblock_flags_mask(struct page *page,
		unsigned long pfn,
		unsigned long end_bitidx,
		unsigned long mask)
{
	WARN_ON_ONCE(!get_pfnblock_flags_mask_sym);
	if (get_pfnblock_flags_mask_sym) {
		struct irq_ibt_state irq_ibt_state;
		unsigned long ret;

		irq_ibt_state = wrapper_irq_ibt_save();
		ret = get_pfnblock_flags_mask_sym(page, pfn, end_bitidx, mask);
		wrapper_irq_ibt_restore(irq_ibt_state);
		return ret;
	} else {
		return -ENOSYS;
	}
}
#endif /* #else #if (LTTNG_LINUX_VERSION_CODE >= LTTNG_KERNEL_VERSION(5,9,0)) */

EXPORT_SYMBOL_GPL(wrapper_get_pfnblock_flags_mask);

int wrapper_get_pfnblock_flags_mask_init(void)
{
	get_pfnblock_flags_mask_sym =
		(void *) kallsyms_lookup_funcptr("get_pfnblock_flags_mask");
	if (!get_pfnblock_flags_mask_sym)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(wrapper_get_pfnblock_flags_mask_init);

#else

#include <linux/pageblock-flags.h>

#endif

#if (defined(CONFIG_KALLSYMS) \
	&& LTTNG_UBUNTU_KERNEL_RANGE(3,13,11,50, 3,14,0,0))

#include <linux/kallsyms.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <wrapper/kallsyms.h>
#include <wrapper/page_alloc.h>

static
unsigned long (*get_pageblock_flags_mask_sym)(struct page *page,
		unsigned long end_bitidx,
		unsigned long mask);

unsigned long wrapper_get_pageblock_flags_mask(struct page *page,
		unsigned long end_bitidx,
		unsigned long mask)
{
	WARN_ON_ONCE(!get_pageblock_flags_mask_sym);
	if (get_pageblock_flags_mask_sym) {
		return get_pageblock_flags_mask_sym(page, end_bitidx, mask);
	} else {
		return -ENOSYS;
	}
}
EXPORT_SYMBOL_GPL(wrapper_get_pageblock_flags_mask);

int wrapper_get_pageblock_flags_mask_init(void)
{
	get_pageblock_flags_mask_sym =
		(void *) kallsyms_lookup_funcptr("get_pageblock_flags_mask");
	if (!get_pageblock_flags_mask_sym)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(wrapper_get_pfnblock_flags_mask_init);

#else

#include <linux/pageblock-flags.h>

#endif
