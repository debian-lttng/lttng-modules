#undef TRACE_SYSTEM
#define TRACE_SYSTEM compaction

#if !defined(_TRACE_COMPACTION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COMPACTION_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/version.h>
#include <trace/events/gfpflags.h>

DECLARE_EVENT_CLASS(mm_compaction_isolate_template,

	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken),

	TP_STRUCT__entry(
		__field(unsigned long, nr_scanned)
		__field(unsigned long, nr_taken)
	),

	TP_fast_assign(
		tp_assign(nr_scanned, nr_scanned)
		tp_assign(nr_taken, nr_taken)
	),

	TP_printk("nr_scanned=%lu nr_taken=%lu",
		__entry->nr_scanned,
		__entry->nr_taken)
)

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_migratepages,

	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken)
)

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_freepages,
	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))
TRACE_EVENT(mm_compaction_migratepages,

	TP_PROTO(unsigned long nr_all,
		int migrate_rc,
		struct list_head *migratepages),

	TP_ARGS(nr_all, migrate_rc, migratepages),

	TP_STRUCT__entry(
		__field(unsigned long, nr_migrated)
		__field(unsigned long, nr_failed)
	),

	TP_fast_assign(
		tp_assign(nr_migrated,
			nr_all -
			(migrate_rc >= 0 ? migrate_rc :
				({
					unsigned long nr_failed = 0;
					struct list_head *page_lru;

					list_for_each(page_lru, migratepages)
						nr_failed++;
					nr_failed;
				})))
		tp_assign(nr_failed,
				({
					unsigned long nr_failed = 0;
					struct list_head *page_lru;

					list_for_each(page_lru, migratepages)
						nr_failed++;
					nr_failed;
				}))
	),

	TP_printk("nr_migrated=%lu nr_failed=%lu",
		__entry->nr_migrated,
		__entry->nr_failed)
)
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)) */
TRACE_EVENT(mm_compaction_migratepages,

	TP_PROTO(unsigned long nr_migrated,
		unsigned long nr_failed),

	TP_ARGS(nr_migrated, nr_failed),

	TP_STRUCT__entry(
		__field(unsigned long, nr_migrated)
		__field(unsigned long, nr_failed)
	),

	TP_fast_assign(
		tp_assign(nr_migrated, nr_migrated)
		tp_assign(nr_failed, nr_failed)
	),

	TP_printk("nr_migrated=%lu nr_failed=%lu",
		__entry->nr_migrated,
		__entry->nr_failed)
)
#endif /* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)) */

#endif /* _TRACE_COMPACTION_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
