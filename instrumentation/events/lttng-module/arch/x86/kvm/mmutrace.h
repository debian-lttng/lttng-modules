#if !defined(LTTNG_TRACE_KVMMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define LTTNG_TRACE_KVMMMU_H

#include "../../../../../../probes/lttng-tracepoint-event.h"
#include <linux/ftrace_event.h>
#include <linux/version.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvmmmu

#undef KVM_MMU_PAGE_FIELDS
#undef KVM_MMU_PAGE_ASSIGN

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

#define KVM_MMU_PAGE_FIELDS \
	__field(unsigned long, mmu_valid_gen) \
	__field(__u64, gfn) \
	__field(__u32, role) \
	__field(__u32, root_count) \
	__field(bool, unsync)

#define KVM_MMU_PAGE_ASSIGN(sp)			     \
	tp_assign(mmu_valid_gen, sp->mmu_valid_gen)  \
	tp_assign(gfn, sp->gfn)			     \
	tp_assign(role, sp->role.word)		     \
	tp_assign(root_count, sp->root_count)        \
	tp_assign(unsync, sp->unsync)

#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)) */

#define KVM_MMU_PAGE_FIELDS \
	__field(__u64, gfn) \
	__field(__u32, role) \
	__field(__u32, root_count) \
	__field(bool, unsync)

#define KVM_MMU_PAGE_ASSIGN(sp)			     \
	tp_assign(gfn, sp->gfn)			     \
	tp_assign(role, sp->role.word)		     \
	tp_assign(root_count, sp->root_count)        \
	tp_assign(unsync, sp->unsync)

#endif /* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)) */

#define kvm_mmu_trace_pferr_flags       \
	{ PFERR_PRESENT_MASK, "P" },	\
	{ PFERR_WRITE_MASK, "W" },	\
	{ PFERR_USER_MASK, "U" },	\
	{ PFERR_RSVD_MASK, "RSVD" },	\
	{ PFERR_FETCH_MASK, "F" }

/*
 * A pagetable walk has started
 */
LTTNG_TRACEPOINT_EVENT(
	kvm_mmu_pagetable_walk,
	TP_PROTO(u64 addr, u32 pferr),
	TP_ARGS(addr, pferr),

	TP_STRUCT__entry(
		__field(__u64, addr)
		__field(__u32, pferr)
	),

	TP_fast_assign(
		tp_assign(addr, addr)
		tp_assign(pferr, pferr)
	),

	TP_printk("addr %llx pferr %x %s", __entry->addr, __entry->pferr,
		  __print_flags(__entry->pferr, "|", kvm_mmu_trace_pferr_flags))
)


/* We just walked a paging element */
LTTNG_TRACEPOINT_EVENT(
	kvm_mmu_paging_element,
	TP_PROTO(u64 pte, int level),
	TP_ARGS(pte, level),

	TP_STRUCT__entry(
		__field(__u64, pte)
		__field(__u32, level)
		),

	TP_fast_assign(
		tp_assign(pte, pte)
		tp_assign(level, level)
		),

	TP_printk("pte %llx level %u", __entry->pte, __entry->level)
)

LTTNG_TRACEPOINT_EVENT_CLASS(kvm_mmu_set_bit_class,

	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),

	TP_ARGS(table_gfn, index, size),

	TP_STRUCT__entry(
		__field(__u64, gpa)
	),

	TP_fast_assign(
		tp_assign(gpa, ((u64)table_gfn << PAGE_SHIFT)
				+ index * size)
		),

	TP_printk("gpa %llx", __entry->gpa)
)

/* We set a pte accessed bit */
LTTNG_TRACEPOINT_EVENT_INSTANCE(kvm_mmu_set_bit_class, kvm_mmu_set_accessed_bit,

	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),

	TP_ARGS(table_gfn, index, size)
)

/* We set a pte dirty bit */
LTTNG_TRACEPOINT_EVENT_INSTANCE(kvm_mmu_set_bit_class, kvm_mmu_set_dirty_bit,

	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),

	TP_ARGS(table_gfn, index, size)
)

LTTNG_TRACEPOINT_EVENT(
	kvm_mmu_walker_error,
	TP_PROTO(u32 pferr),
	TP_ARGS(pferr),

	TP_STRUCT__entry(
		__field(__u32, pferr)
		),

	TP_fast_assign(
		tp_assign(pferr, pferr)
		),

	TP_printk("pferr %x %s", __entry->pferr,
		  __print_flags(__entry->pferr, "|", kvm_mmu_trace_pferr_flags))
)

LTTNG_TRACEPOINT_EVENT(
	kvm_mmu_get_page,
	TP_PROTO(struct kvm_mmu_page *sp, bool created),
	TP_ARGS(sp, created),

	TP_STRUCT__entry(
		KVM_MMU_PAGE_FIELDS
		__field(bool, created)
		),

	TP_fast_assign(
		KVM_MMU_PAGE_ASSIGN(sp)
		tp_assign(created, created)
		),

	TP_printk()
)

LTTNG_TRACEPOINT_EVENT_CLASS(kvm_mmu_page_class,

	TP_PROTO(struct kvm_mmu_page *sp),
	TP_ARGS(sp),

	TP_STRUCT__entry(
		KVM_MMU_PAGE_FIELDS
	),

	TP_fast_assign(
		KVM_MMU_PAGE_ASSIGN(sp)
	),

	TP_printk()
)

LTTNG_TRACEPOINT_EVENT_INSTANCE(kvm_mmu_page_class, kvm_mmu_sync_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
)

LTTNG_TRACEPOINT_EVENT_INSTANCE(kvm_mmu_page_class, kvm_mmu_unsync_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
)

LTTNG_TRACEPOINT_EVENT_INSTANCE(kvm_mmu_page_class, kvm_mmu_prepare_zap_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0))

LTTNG_TRACEPOINT_EVENT(
	mark_mmio_spte,
	TP_PROTO(u64 *sptep, gfn_t gfn, unsigned access, unsigned int gen),
	TP_ARGS(sptep, gfn, access, gen),

	TP_STRUCT__entry(
		__field(void *, sptep)
		__field(gfn_t, gfn)
		__field(unsigned, access)
		__field(unsigned int, gen)
	),

	TP_fast_assign(
		tp_assign(sptep, sptep)
		tp_assign(gfn, gfn)
		tp_assign(access, access)
		tp_assign(gen, gen)
	),

	TP_printk("sptep:%p gfn %llx access %x", __entry->sptep, __entry->gfn,
		  __entry->access)
)

#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)) */

LTTNG_TRACEPOINT_EVENT(
	mark_mmio_spte,
	TP_PROTO(u64 *sptep, gfn_t gfn, unsigned access),
	TP_ARGS(sptep, gfn, access),

	TP_STRUCT__entry(
		__field(void *, sptep)
		__field(gfn_t, gfn)
		__field(unsigned, access)
	),

	TP_fast_assign(
		tp_assign(sptep, sptep)
		tp_assign(gfn, gfn)
		tp_assign(access, access)
	),

	TP_printk("sptep:%p gfn %llx access %x", __entry->sptep, __entry->gfn,
		  __entry->access)
)

#endif /* #else #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)) */

LTTNG_TRACEPOINT_EVENT(
	handle_mmio_page_fault,
	TP_PROTO(u64 addr, gfn_t gfn, unsigned access),
	TP_ARGS(addr, gfn, access),

	TP_STRUCT__entry(
		__field(u64, addr)
		__field(gfn_t, gfn)
		__field(unsigned, access)
	),

	TP_fast_assign(
		tp_assign(addr, addr)
		tp_assign(gfn, gfn)
		tp_assign(access, access)
	),

	TP_printk("addr:%llx gfn %llx access %x", __entry->addr, __entry->gfn,
		  __entry->access)
)

#define __spte_satisfied(__spte)				\
	(__entry->retry && is_writable_pte(__entry->__spte))

LTTNG_TRACEPOINT_EVENT(
	fast_page_fault,
	TP_PROTO(struct kvm_vcpu *vcpu, gva_t gva, u32 error_code,
		 u64 *sptep, u64 old_spte, bool retry),
	TP_ARGS(vcpu, gva, error_code, sptep, old_spte, retry),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(gva_t, gva)
		__field(u32, error_code)
		__field(u64 *, sptep)
		__field(u64, old_spte)
		__field(u64, new_spte)
		__field(bool, retry)
	),

	TP_fast_assign(
		tp_assign(vcpu_id, vcpu->vcpu_id)
		tp_assign(gva, gva)
		tp_assign(error_code, error_code)
		tp_assign(sptep, sptep)
		tp_assign(old_spte, old_spte)
		tp_assign(new_spte, *sptep)
		tp_assign(retry, retry)
	),

	TP_printk("vcpu %d gva %lx error_code %s sptep %p old %#llx"
		  " new %llx spurious %d fixed %d", __entry->vcpu_id,
		  __entry->gva, __print_flags(__entry->error_code, "|",
		  kvm_mmu_trace_pferr_flags), __entry->sptep,
		  __entry->old_spte, __entry->new_spte,
		  __spte_satisfied(old_spte), __spte_satisfied(new_spte)
	)
)
#endif /* LTTNG_TRACE_KVMMMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../instrumentation/events/lttng-module/arch/x86/kvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mmutrace

/* This part must be outside protection */
#include "../../../../../../probes/define_trace.h"
