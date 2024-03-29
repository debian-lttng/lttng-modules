/* SPDX-License-Identifier: MIT
 *
 * lttng-bytecode-validator.c
 *
 * LTTng modules bytecode bytecode validator.
 *
 * Copyright (C) 2010-2016 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#include <linux/types.h>
#include <linux/jhash.h>
#include <linux/slab.h>

#include <wrapper/list.h>
#include <lttng/lttng-bytecode.h>

#define MERGE_POINT_TABLE_BITS		7
#define MERGE_POINT_TABLE_SIZE		(1U << MERGE_POINT_TABLE_BITS)

/* merge point table node */
struct mp_node {
	struct hlist_node node;

	/* Context at merge point */
	struct vstack stack;
	unsigned long target_pc;
};

struct mp_table {
	struct hlist_head mp_head[MERGE_POINT_TABLE_SIZE];
};

static
int lttng_hash_match(struct mp_node *mp_node, unsigned long key_pc)
{
	if (mp_node->target_pc == key_pc)
		return 1;
	else
		return 0;
}

static
int merge_points_compare(const struct vstack *stacka,
			const struct vstack *stackb)
{
	int i, len;

	if (stacka->top != stackb->top)
		return 1;
	len = stacka->top + 1;
	WARN_ON_ONCE(len < 0);
	for (i = 0; i < len; i++) {
		if (stacka->e[i].type != stackb->e[i].type)
			return 1;
	}
	return 0;
}

static
int merge_point_add_check(struct mp_table *mp_table, unsigned long target_pc,
		const struct vstack *stack)
{
	struct mp_node *mp_node;
	unsigned long hash = jhash_1word(target_pc, 0);
	struct hlist_head *head;
	struct mp_node *lookup_node;
	int found = 0;

	dbg_printk("Bytecode: adding merge point at offset %lu, hash %lu\n",
			target_pc, hash);
	mp_node = kzalloc(sizeof(struct mp_node), GFP_KERNEL);
	if (!mp_node)
		return -ENOMEM;
	mp_node->target_pc = target_pc;
	memcpy(&mp_node->stack, stack, sizeof(mp_node->stack));

	head = &mp_table->mp_head[hash & (MERGE_POINT_TABLE_SIZE - 1)];
	lttng_hlist_for_each_entry(lookup_node, head, node) {
		if (lttng_hash_match(lookup_node, target_pc)) {
			found = 1;
			break;
		}
	}
	if (found) {
		/* Key already present */
		dbg_printk("Bytecode: compare merge points for offset %lu, hash %lu\n",
				target_pc, hash);
		kfree(mp_node);
		if (merge_points_compare(stack, &lookup_node->stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Merge points differ for offset %lu\n",
				target_pc);
			return -EINVAL;
		}
	} else {
		hlist_add_head(&mp_node->node, head);
	}
	return 0;
}

/*
 * Binary comparators use top of stack and top of stack -1.
 */
static
int bin_op_compare_check(struct vstack *stack, const bytecode_opcode_t opcode,
		const char *str)
{
	if (unlikely(!vstack_ax(stack) || !vstack_bx(stack)))
		goto error_empty;

	switch (vstack_ax(stack)->type) {
	default:
	case REG_DOUBLE:
		goto error_type;

	case REG_STRING:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
			goto unknown;
		case REG_STRING:
			break;
		case REG_STAR_GLOB_STRING:
			if (opcode != BYTECODE_OP_EQ && opcode != BYTECODE_OP_NE) {
				goto error_mismatch;
			}
			break;
		case REG_S64:
		case REG_U64:
			goto error_mismatch;
		}
		break;
	case REG_STAR_GLOB_STRING:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
			goto unknown;
		case REG_STRING:
			if (opcode != BYTECODE_OP_EQ && opcode != BYTECODE_OP_NE) {
				goto error_mismatch;
			}
			break;
		case REG_STAR_GLOB_STRING:
		case REG_S64:
		case REG_U64:
			goto error_mismatch;
		}
		break;
	case REG_S64:
	case REG_U64:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
			goto unknown;
		case REG_STRING:
		case REG_STAR_GLOB_STRING:
			goto error_mismatch;
		case REG_S64:
		case REG_U64:
			break;
		}
		break;
	case REG_TYPE_UNKNOWN:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
		case REG_STRING:
		case REG_STAR_GLOB_STRING:
		case REG_S64:
		case REG_U64:
			goto unknown;
		}
		break;
	}
	return 0;

unknown:
	return 1;

error_empty:
	printk(KERN_WARNING "LTTng: bytecode: empty stack for '%s' binary operator\n", str);
	return -EINVAL;

error_mismatch:
	printk(KERN_WARNING "LTTng: bytecode: type mismatch for '%s' binary operator\n", str);
	return -EINVAL;

error_type:
	printk(KERN_WARNING "LTTng: bytecode: unknown type for '%s' binary operator\n", str);
	return -EINVAL;
}

/*
 * Binary bitwise operators use top of stack and top of stack -1.
 * Return 0 if typing is known to match, 1 if typing is dynamic
 * (unknown), negative error value on error.
 */
static
int bin_op_bitwise_check(struct vstack *stack, bytecode_opcode_t opcode,
		const char *str)
{
	if (unlikely(!vstack_ax(stack) || !vstack_bx(stack)))
		goto error_empty;

	switch (vstack_ax(stack)->type) {
	default:
	case REG_DOUBLE:
		goto error_type;

	case REG_TYPE_UNKNOWN:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
		case REG_S64:
		case REG_U64:
			goto unknown;
		}
		break;
	case REG_S64:
	case REG_U64:
		switch (vstack_bx(stack)->type) {
		default:
		case REG_DOUBLE:
			goto error_type;
		case REG_TYPE_UNKNOWN:
			goto unknown;
		case REG_S64:
		case REG_U64:
			break;
		}
		break;
	}
	return 0;

unknown:
	return 1;

error_empty:
	printk(KERN_WARNING "LTTng: bytecode: empty stack for '%s' binary operator\n", str);
	return -EINVAL;

error_type:
	printk(KERN_WARNING "LTTng: bytecode: unknown type for '%s' binary operator\n", str);
	return -EINVAL;
}

static
int validate_get_symbol(struct bytecode_runtime *bytecode,
		const struct get_symbol *sym)
{
	const char *str, *str_limit;
	size_t len_limit;

	if (sym->offset >= bytecode->p.bc->bc.len - bytecode->p.bc->bc.reloc_offset)
		return -EINVAL;

	str = bytecode->p.bc->bc.data + bytecode->p.bc->bc.reloc_offset + sym->offset;
	str_limit = bytecode->p.bc->bc.data + bytecode->p.bc->bc.len;
	len_limit = str_limit - str;
	if (strnlen(str, len_limit) == len_limit)
		return -EINVAL;
	return 0;
}

/*
 * Validate bytecode range overflow within the validation pass.
 * Called for each instruction encountered.
 */
static
int bytecode_validate_overflow(struct bytecode_runtime *bytecode,
		char *start_pc, char *pc)
{
	int ret = 0;

	switch (*(bytecode_opcode_t *) pc) {
	case BYTECODE_OP_UNKNOWN:
	default:
	{
		printk(KERN_WARNING "LTTng: bytecode: unknown bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		break;
	}

	case BYTECODE_OP_RETURN:
	case BYTECODE_OP_RETURN_S64:
	{
		if (unlikely(pc + sizeof(struct return_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/* binary */
	case BYTECODE_OP_MUL:
	case BYTECODE_OP_DIV:
	case BYTECODE_OP_MOD:
	case BYTECODE_OP_PLUS:
	case BYTECODE_OP_MINUS:
	case BYTECODE_OP_EQ_DOUBLE:
	case BYTECODE_OP_NE_DOUBLE:
	case BYTECODE_OP_GT_DOUBLE:
	case BYTECODE_OP_LT_DOUBLE:
	case BYTECODE_OP_GE_DOUBLE:
	case BYTECODE_OP_LE_DOUBLE:
	/* Floating point */
	case BYTECODE_OP_EQ_DOUBLE_S64:
	case BYTECODE_OP_NE_DOUBLE_S64:
	case BYTECODE_OP_GT_DOUBLE_S64:
	case BYTECODE_OP_LT_DOUBLE_S64:
	case BYTECODE_OP_GE_DOUBLE_S64:
	case BYTECODE_OP_LE_DOUBLE_S64:
	case BYTECODE_OP_EQ_S64_DOUBLE:
	case BYTECODE_OP_NE_S64_DOUBLE:
	case BYTECODE_OP_GT_S64_DOUBLE:
	case BYTECODE_OP_LT_S64_DOUBLE:
	case BYTECODE_OP_GE_S64_DOUBLE:
	case BYTECODE_OP_LE_S64_DOUBLE:
	case BYTECODE_OP_LOAD_FIELD_REF_DOUBLE:
	case BYTECODE_OP_GET_CONTEXT_REF_DOUBLE:
	case BYTECODE_OP_LOAD_DOUBLE:
	case BYTECODE_OP_CAST_DOUBLE_TO_S64:
	case BYTECODE_OP_UNARY_PLUS_DOUBLE:
	case BYTECODE_OP_UNARY_MINUS_DOUBLE:
	case BYTECODE_OP_UNARY_NOT_DOUBLE:
	{
		printk(KERN_WARNING "LTTng: bytecode: unsupported bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		break;
	}

	case BYTECODE_OP_EQ:
	case BYTECODE_OP_NE:
	case BYTECODE_OP_GT:
	case BYTECODE_OP_LT:
	case BYTECODE_OP_GE:
	case BYTECODE_OP_LE:
	case BYTECODE_OP_EQ_STRING:
	case BYTECODE_OP_NE_STRING:
	case BYTECODE_OP_GT_STRING:
	case BYTECODE_OP_LT_STRING:
	case BYTECODE_OP_GE_STRING:
	case BYTECODE_OP_LE_STRING:
	case BYTECODE_OP_EQ_STAR_GLOB_STRING:
	case BYTECODE_OP_NE_STAR_GLOB_STRING:
	case BYTECODE_OP_EQ_S64:
	case BYTECODE_OP_NE_S64:
	case BYTECODE_OP_GT_S64:
	case BYTECODE_OP_LT_S64:
	case BYTECODE_OP_GE_S64:
	case BYTECODE_OP_LE_S64:
	case BYTECODE_OP_BIT_RSHIFT:
	case BYTECODE_OP_BIT_LSHIFT:
	case BYTECODE_OP_BIT_AND:
	case BYTECODE_OP_BIT_OR:
	case BYTECODE_OP_BIT_XOR:
	{
		if (unlikely(pc + sizeof(struct binary_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/* unary */
	case BYTECODE_OP_UNARY_PLUS:
	case BYTECODE_OP_UNARY_MINUS:
	case BYTECODE_OP_UNARY_NOT:
	case BYTECODE_OP_UNARY_PLUS_S64:
	case BYTECODE_OP_UNARY_MINUS_S64:
	case BYTECODE_OP_UNARY_NOT_S64:
	case BYTECODE_OP_UNARY_BIT_NOT:
	{
		if (unlikely(pc + sizeof(struct unary_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/* logical */
	case BYTECODE_OP_AND:
	case BYTECODE_OP_OR:
	{
		if (unlikely(pc + sizeof(struct logical_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/* load field and get context ref */
	case BYTECODE_OP_LOAD_FIELD_REF:
	case BYTECODE_OP_GET_CONTEXT_REF:
	case BYTECODE_OP_LOAD_FIELD_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_REF_S64:
	case BYTECODE_OP_GET_CONTEXT_REF_STRING:
	case BYTECODE_OP_GET_CONTEXT_REF_S64:
	{
		if (unlikely(pc + sizeof(struct load_op) + sizeof(struct field_ref)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/* load from immediate operand */
	case BYTECODE_OP_LOAD_STRING:
	case BYTECODE_OP_LOAD_STAR_GLOB_STRING:
	{
		struct load_op *insn = (struct load_op *) pc;
		uint32_t str_len, maxlen;

		if (unlikely(pc + sizeof(struct load_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
			break;
		}

		maxlen = start_pc + bytecode->len - pc - sizeof(struct load_op);
		str_len = strnlen(insn->data, maxlen);
		if (unlikely(str_len >= maxlen)) {
			/* Final '\0' not found within range */
			ret = -ERANGE;
		}
		break;
	}

	case BYTECODE_OP_LOAD_S64:
	{
		if (unlikely(pc + sizeof(struct load_op) + sizeof(struct literal_numeric)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	case BYTECODE_OP_CAST_TO_S64:
	case BYTECODE_OP_CAST_NOP:
	{
		if (unlikely(pc + sizeof(struct cast_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	/*
	 * Instructions for recursive traversal through composed types.
	 */
	case BYTECODE_OP_GET_CONTEXT_ROOT:
	case BYTECODE_OP_GET_APP_CONTEXT_ROOT:
	case BYTECODE_OP_GET_PAYLOAD_ROOT:
	case BYTECODE_OP_LOAD_FIELD:
	case BYTECODE_OP_LOAD_FIELD_S8:
	case BYTECODE_OP_LOAD_FIELD_S16:
	case BYTECODE_OP_LOAD_FIELD_S32:
	case BYTECODE_OP_LOAD_FIELD_S64:
	case BYTECODE_OP_LOAD_FIELD_U8:
	case BYTECODE_OP_LOAD_FIELD_U16:
	case BYTECODE_OP_LOAD_FIELD_U32:
	case BYTECODE_OP_LOAD_FIELD_U64:
	case BYTECODE_OP_LOAD_FIELD_STRING:
	case BYTECODE_OP_LOAD_FIELD_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_DOUBLE:
		if (unlikely(pc + sizeof(struct load_op)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;

	case BYTECODE_OP_GET_SYMBOL:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct get_symbol *sym = (struct get_symbol *) insn->data;

		if (unlikely(pc + sizeof(struct load_op) + sizeof(struct get_symbol)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
			break;
		}
		ret = validate_get_symbol(bytecode, sym);
		break;
	}

	case BYTECODE_OP_GET_SYMBOL_FIELD:
		printk(KERN_WARNING "LTTng: bytecode: Unexpected get symbol field\n");
		ret = -EINVAL;
		break;

	case BYTECODE_OP_GET_INDEX_U16:
		if (unlikely(pc + sizeof(struct load_op) + sizeof(struct get_index_u16)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;

	case BYTECODE_OP_GET_INDEX_U64:
		if (unlikely(pc + sizeof(struct load_op) + sizeof(struct get_index_u64)
				> start_pc + bytecode->len)) {
			ret = -ERANGE;
		}
		break;
	}

	return ret;
}

static
unsigned long delete_all_nodes(struct mp_table *mp_table)
{
	struct mp_node *mp_node;
	struct hlist_node *tmp;
	unsigned long nr_nodes = 0;
	int i;

	for (i = 0; i < MERGE_POINT_TABLE_SIZE; i++) {
		struct hlist_head *head;

		head = &mp_table->mp_head[i];
		lttng_hlist_for_each_entry_safe(mp_node, tmp, head, node) {
			kfree(mp_node);
			nr_nodes++;
		}
	}
	return nr_nodes;
}

/*
 * Return value:
 * >=0: success
 * <0: error
 */
static
int validate_instruction_context(struct bytecode_runtime *bytecode,
		struct vstack *stack,
		char *start_pc,
		char *pc)
{
	int ret = 0;
	const bytecode_opcode_t opcode = *(bytecode_opcode_t *) pc;

	switch (opcode) {
	case BYTECODE_OP_UNKNOWN:
	default:
	{
		printk(KERN_WARNING "LTTng: bytecode: unknown bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_RETURN:
	case BYTECODE_OP_RETURN_S64:
	{
		goto end;
	}

	/* binary */
	case BYTECODE_OP_MUL:
	case BYTECODE_OP_DIV:
	case BYTECODE_OP_MOD:
	case BYTECODE_OP_PLUS:
	case BYTECODE_OP_MINUS:
	/* Floating point */
	case BYTECODE_OP_EQ_DOUBLE:
	case BYTECODE_OP_NE_DOUBLE:
	case BYTECODE_OP_GT_DOUBLE:
	case BYTECODE_OP_LT_DOUBLE:
	case BYTECODE_OP_GE_DOUBLE:
	case BYTECODE_OP_LE_DOUBLE:
	case BYTECODE_OP_EQ_DOUBLE_S64:
	case BYTECODE_OP_NE_DOUBLE_S64:
	case BYTECODE_OP_GT_DOUBLE_S64:
	case BYTECODE_OP_LT_DOUBLE_S64:
	case BYTECODE_OP_GE_DOUBLE_S64:
	case BYTECODE_OP_LE_DOUBLE_S64:
	case BYTECODE_OP_EQ_S64_DOUBLE:
	case BYTECODE_OP_NE_S64_DOUBLE:
	case BYTECODE_OP_GT_S64_DOUBLE:
	case BYTECODE_OP_LT_S64_DOUBLE:
	case BYTECODE_OP_GE_S64_DOUBLE:
	case BYTECODE_OP_LE_S64_DOUBLE:
	case BYTECODE_OP_UNARY_PLUS_DOUBLE:
	case BYTECODE_OP_UNARY_MINUS_DOUBLE:
	case BYTECODE_OP_UNARY_NOT_DOUBLE:
	case BYTECODE_OP_LOAD_FIELD_REF_DOUBLE:
	case BYTECODE_OP_LOAD_DOUBLE:
	case BYTECODE_OP_CAST_DOUBLE_TO_S64:
	case BYTECODE_OP_GET_CONTEXT_REF_DOUBLE:
	{
		printk(KERN_WARNING "LTTng: bytecode: unsupported bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_EQ:
	{
		ret = bin_op_compare_check(stack, opcode, "==");
		if (ret < 0)
			goto end;
		break;
	}
	case BYTECODE_OP_NE:
	{
		ret = bin_op_compare_check(stack, opcode, "!=");
		if (ret < 0)
			goto end;
		break;
	}
	case BYTECODE_OP_GT:
	{
		ret = bin_op_compare_check(stack, opcode, ">");
		if (ret < 0)
			goto end;
		break;
	}
	case BYTECODE_OP_LT:
	{
		ret = bin_op_compare_check(stack, opcode, "<");
		if (ret < 0)
			goto end;
		break;
	}
	case BYTECODE_OP_GE:
	{
		ret = bin_op_compare_check(stack, opcode, ">=");
		if (ret < 0)
			goto end;
		break;
	}
	case BYTECODE_OP_LE:
	{
		ret = bin_op_compare_check(stack, opcode, "<=");
		if (ret < 0)
			goto end;
		break;
	}

	case BYTECODE_OP_EQ_STRING:
	case BYTECODE_OP_NE_STRING:
	case BYTECODE_OP_GT_STRING:
	case BYTECODE_OP_LT_STRING:
	case BYTECODE_OP_GE_STRING:
	case BYTECODE_OP_LE_STRING:
	{
		if (!vstack_ax(stack) || !vstack_bx(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_STRING
				|| vstack_bx(stack)->type != REG_STRING) {
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type for string comparator\n");
			ret = -EINVAL;
			goto end;
		}
		break;
	}


	case BYTECODE_OP_EQ_STAR_GLOB_STRING:
	case BYTECODE_OP_NE_STAR_GLOB_STRING:
	{
		if (!vstack_ax(stack) || !vstack_bx(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_STAR_GLOB_STRING
				&& vstack_bx(stack)->type != REG_STAR_GLOB_STRING) {
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type for globbing pattern comparator\n");
			ret = -EINVAL;
			goto end;
		}
		break;
	}

	case BYTECODE_OP_EQ_S64:
	case BYTECODE_OP_NE_S64:
	case BYTECODE_OP_GT_S64:
	case BYTECODE_OP_LT_S64:
	case BYTECODE_OP_GE_S64:
	case BYTECODE_OP_LE_S64:
	{
		if (!vstack_ax(stack) || !vstack_bx(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type for s64 comparator\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_bx(stack)->type) {
		case REG_S64:
		case REG_U64:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type for s64 comparator\n");
			ret = -EINVAL;
			goto end;
		}
		break;
	}

	case BYTECODE_OP_BIT_RSHIFT:
		ret = bin_op_bitwise_check(stack, opcode, ">>");
		if (ret < 0)
			goto end;
		break;
	case BYTECODE_OP_BIT_LSHIFT:
		ret = bin_op_bitwise_check(stack, opcode, "<<");
		if (ret < 0)
			goto end;
		break;
	case BYTECODE_OP_BIT_AND:
		ret = bin_op_bitwise_check(stack, opcode, "&");
		if (ret < 0)
			goto end;
		break;
	case BYTECODE_OP_BIT_OR:
		ret = bin_op_bitwise_check(stack, opcode, "|");
		if (ret < 0)
			goto end;
		break;
	case BYTECODE_OP_BIT_XOR:
		ret = bin_op_bitwise_check(stack, opcode, "^");
		if (ret < 0)
			goto end;
		break;

	/* unary */
	case BYTECODE_OP_UNARY_PLUS:
	case BYTECODE_OP_UNARY_MINUS:
	case BYTECODE_OP_UNARY_NOT:
	{
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		default:
		case REG_DOUBLE:
			printk(KERN_WARNING "LTTng: bytecode: unknown register type\n");
			ret = -EINVAL;
			goto end;

		case REG_STRING:
		case REG_STAR_GLOB_STRING:
			printk(KERN_WARNING "LTTng: bytecode: Unary op can only be applied to numeric or floating point registers\n");
			ret = -EINVAL;
			goto end;
		case REG_S64:
		case REG_U64:
		case REG_TYPE_UNKNOWN:
			break;
		}
		break;
	}
	case BYTECODE_OP_UNARY_BIT_NOT:
	{
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		default:
			printk(KERN_WARNING "LTTng: bytecode: unknown register type\n");
			ret = -EINVAL;
			goto end;

		case REG_STRING:
		case REG_STAR_GLOB_STRING:
		case REG_DOUBLE:
			printk(KERN_WARNING "LTTng: bytecode: Unary bitwise op can only be applied to numeric registers\n");
			ret = -EINVAL;
			goto end;
		case REG_S64:
		case REG_U64:
		case REG_TYPE_UNKNOWN:
			break;
		}
		break;
	}

	case BYTECODE_OP_UNARY_PLUS_S64:
	case BYTECODE_OP_UNARY_MINUS_S64:
	case BYTECODE_OP_UNARY_NOT_S64:
	{
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_S64 &&
				vstack_ax(stack)->type != REG_U64) {
			printk(KERN_WARNING "LTTng: bytecode: Invalid register type\n");
			ret = -EINVAL;
			goto end;
		}
		break;
	}

	/* logical */
	case BYTECODE_OP_AND:
	case BYTECODE_OP_OR:
	{
		struct logical_op *insn = (struct logical_op *) pc;

		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_S64 &&
				vstack_ax(stack)->type != REG_U64) {
			printk(KERN_WARNING "LTTng: bytecode: Logical comparator expects S64 register\n");
			ret = -EINVAL;
			goto end;
		}

		dbg_printk("Validate jumping to bytecode offset %u\n",
			(unsigned int) insn->skip_offset);
		if (unlikely(start_pc + insn->skip_offset <= pc)) {
			printk(KERN_WARNING "LTTng: bytecode: Loops are not allowed in bytecode\n");
			ret = -EINVAL;
			goto end;
		}
		break;
	}

	/* load field ref */
	case BYTECODE_OP_LOAD_FIELD_REF:
	{
		printk(KERN_WARNING "LTTng: bytecode: Unknown field ref type\n");
		ret = -EINVAL;
		goto end;
	}
	case BYTECODE_OP_LOAD_FIELD_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_SEQUENCE:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct field_ref *ref = (struct field_ref *) insn->data;

		dbg_printk("Validate load field ref offset %u type string\n",
			ref->offset);
		break;
	}
	case BYTECODE_OP_LOAD_FIELD_REF_S64:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct field_ref *ref = (struct field_ref *) insn->data;

		dbg_printk("Validate load field ref offset %u type s64\n",
			ref->offset);
		break;
	}

	/* load from immediate operand */
	case BYTECODE_OP_LOAD_STRING:
	case BYTECODE_OP_LOAD_STAR_GLOB_STRING:
	{
		break;
	}

	case BYTECODE_OP_LOAD_S64:
	{
		break;
	}

	case BYTECODE_OP_CAST_TO_S64:
	{
		struct cast_op *insn = (struct cast_op *) pc;

		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		default:
		case REG_DOUBLE:
			printk(KERN_WARNING "LTTng: bytecode: unknown register type\n");
			ret = -EINVAL;
			goto end;

		case REG_STRING:
		case REG_STAR_GLOB_STRING:
			printk(KERN_WARNING "LTTng: bytecode: Cast op can only be applied to numeric or floating point registers\n");
			ret = -EINVAL;
			goto end;
		case REG_S64:
			break;
		}
		if (insn->op == BYTECODE_OP_CAST_DOUBLE_TO_S64) {
			if (vstack_ax(stack)->type != REG_DOUBLE) {
				printk(KERN_WARNING "LTTng: bytecode: Cast expects double\n");
				ret = -EINVAL;
				goto end;
			}
		}
		break;
	}
	case BYTECODE_OP_CAST_NOP:
	{
		break;
	}

	/* get context ref */
	case BYTECODE_OP_GET_CONTEXT_REF:
	{
		printk(KERN_WARNING "LTTng: bytecode: Unknown get context ref type\n");
		ret = -EINVAL;
		goto end;
	}
	case BYTECODE_OP_GET_CONTEXT_REF_STRING:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct field_ref *ref = (struct field_ref *) insn->data;

		dbg_printk("Validate get context ref offset %u type string\n",
			ref->offset);
		break;
	}
	case BYTECODE_OP_GET_CONTEXT_REF_S64:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct field_ref *ref = (struct field_ref *) insn->data;

		dbg_printk("Validate get context ref offset %u type s64\n",
			ref->offset);
		break;
	}

	/*
	 * Instructions for recursive traversal through composed types.
	 */
	case BYTECODE_OP_GET_CONTEXT_ROOT:
	{
		dbg_printk("Validate get context root\n");
		break;
	}
	case BYTECODE_OP_GET_APP_CONTEXT_ROOT:
	{
		dbg_printk("Validate get app context root\n");
		break;
	}
	case BYTECODE_OP_GET_PAYLOAD_ROOT:
	{
		dbg_printk("Validate get payload root\n");
		break;
	}
	case BYTECODE_OP_LOAD_FIELD:
	{
		/*
		 * We tolerate that field type is unknown at validation,
		 * because we are performing the load specialization in
		 * a phase after validation.
		 */
		dbg_printk("Validate load field\n");
		break;
	}

	/*
	 * Disallow already specialized bytecode op load field instructions to
	 * ensure that the received bytecode does not:
	 *
	 * - Read user-space memory without proper get_user accessors,
	 * - Read a memory area larger than the memory targeted by the instrumentation.
	 */
	case BYTECODE_OP_LOAD_FIELD_S8:
	case BYTECODE_OP_LOAD_FIELD_S16:
	case BYTECODE_OP_LOAD_FIELD_S32:
	case BYTECODE_OP_LOAD_FIELD_S64:
	case BYTECODE_OP_LOAD_FIELD_U8:
	case BYTECODE_OP_LOAD_FIELD_U16:
	case BYTECODE_OP_LOAD_FIELD_U32:
	case BYTECODE_OP_LOAD_FIELD_U64:
	case BYTECODE_OP_LOAD_FIELD_STRING:
	case BYTECODE_OP_LOAD_FIELD_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_DOUBLE:
	{
		dbg_printk("Validate load field, reject specialized load instruction (%d)\n",
				(int) opcode);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_GET_SYMBOL:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct get_symbol *sym = (struct get_symbol *) insn->data;

		dbg_printk("Validate get symbol offset %u\n", sym->offset);
		break;
	}

	case BYTECODE_OP_GET_SYMBOL_FIELD:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct get_symbol *sym = (struct get_symbol *) insn->data;

		dbg_printk("Validate get symbol field offset %u\n", sym->offset);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U16:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct get_index_u16 *get_index = (struct get_index_u16 *) insn->data;

		dbg_printk("Validate get index u16 index %u\n", get_index->index);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U64:
	{
		struct load_op *insn = (struct load_op *) pc;
		struct get_index_u64 *get_index = (struct get_index_u64 *) insn->data;

		dbg_printk("Validate get index u64 index %llu\n",
			(unsigned long long) get_index->index);
		break;
	}
	}
end:
	return ret;
}

/*
 * Return value:
 * 0: success
 * <0: error
 */
static
int validate_instruction_all_contexts(struct bytecode_runtime *bytecode,
		struct mp_table *mp_table,
		struct vstack *stack,
		char *start_pc,
		char *pc)
{
	int ret, found = 0;
	unsigned long target_pc = pc - start_pc;
	unsigned long hash;
	struct hlist_head *head;
	struct mp_node *mp_node;

	/* Validate the context resulting from the previous instruction */
	ret = validate_instruction_context(bytecode, stack, start_pc, pc);
	if (ret < 0)
		return ret;

	/* Validate merge points */
	hash = jhash_1word(target_pc, 0);
	head = &mp_table->mp_head[hash & (MERGE_POINT_TABLE_SIZE - 1)];
	lttng_hlist_for_each_entry(mp_node, head, node) {
		if (lttng_hash_match(mp_node, target_pc)) {
			found = 1;
			break;
		}
	}
	if (found) {
		dbg_printk("Bytecode: validate merge point at offset %lu\n",
				target_pc);
		if (merge_points_compare(stack, &mp_node->stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Merge points differ for offset %lu\n",
				target_pc);
			return -EINVAL;
		}
		/* Once validated, we can remove the merge point */
		dbg_printk("Bytecode: remove merge point at offset %lu\n",
				target_pc);
		hlist_del(&mp_node->node);
	}
	return 0;
}

/*
 * Validate load instructions: specialized instructions not accepted as input.
 *
 * Return value:
 * >0: going to next insn.
 * 0: success, stop iteration.
 * <0: error
 */
static
int validate_load(char **_next_pc,
		char *pc)
{
	int ret = 0;
	char *next_pc = *_next_pc;

	switch (*(bytecode_opcode_t *) pc) {
	case BYTECODE_OP_UNKNOWN:
	default:
	{
		printk(KERN_WARNING "LTTng: bytecode: unknown bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_RETURN:
	{
		next_pc += sizeof(struct return_op);
		break;
	}

	case BYTECODE_OP_RETURN_S64:
	{
		next_pc += sizeof(struct return_op);
		break;
	}

	/* binary */
	case BYTECODE_OP_MUL:
	case BYTECODE_OP_DIV:
	case BYTECODE_OP_MOD:
	case BYTECODE_OP_PLUS:
	case BYTECODE_OP_MINUS:
	/* Floating point */
	case BYTECODE_OP_EQ_DOUBLE:
	case BYTECODE_OP_NE_DOUBLE:
	case BYTECODE_OP_GT_DOUBLE:
	case BYTECODE_OP_LT_DOUBLE:
	case BYTECODE_OP_GE_DOUBLE:
	case BYTECODE_OP_LE_DOUBLE:
	case BYTECODE_OP_EQ_DOUBLE_S64:
	case BYTECODE_OP_NE_DOUBLE_S64:
	case BYTECODE_OP_GT_DOUBLE_S64:
	case BYTECODE_OP_LT_DOUBLE_S64:
	case BYTECODE_OP_GE_DOUBLE_S64:
	case BYTECODE_OP_LE_DOUBLE_S64:
	case BYTECODE_OP_EQ_S64_DOUBLE:
	case BYTECODE_OP_NE_S64_DOUBLE:
	case BYTECODE_OP_GT_S64_DOUBLE:
	case BYTECODE_OP_LT_S64_DOUBLE:
	case BYTECODE_OP_GE_S64_DOUBLE:
	case BYTECODE_OP_LE_S64_DOUBLE:
	case BYTECODE_OP_UNARY_PLUS_DOUBLE:
	case BYTECODE_OP_UNARY_MINUS_DOUBLE:
	case BYTECODE_OP_UNARY_NOT_DOUBLE:
	case BYTECODE_OP_LOAD_FIELD_REF_DOUBLE:
	case BYTECODE_OP_GET_CONTEXT_REF_DOUBLE:
	case BYTECODE_OP_LOAD_DOUBLE:
	case BYTECODE_OP_CAST_DOUBLE_TO_S64:
	{
		printk(KERN_WARNING "LTTng: bytecode: unsupported bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_EQ:
	case BYTECODE_OP_NE:
	case BYTECODE_OP_GT:
	case BYTECODE_OP_LT:
	case BYTECODE_OP_GE:
	case BYTECODE_OP_LE:
	case BYTECODE_OP_EQ_STRING:
	case BYTECODE_OP_NE_STRING:
	case BYTECODE_OP_GT_STRING:
	case BYTECODE_OP_LT_STRING:
	case BYTECODE_OP_GE_STRING:
	case BYTECODE_OP_LE_STRING:
	case BYTECODE_OP_EQ_STAR_GLOB_STRING:
	case BYTECODE_OP_NE_STAR_GLOB_STRING:
	case BYTECODE_OP_EQ_S64:
	case BYTECODE_OP_NE_S64:
	case BYTECODE_OP_GT_S64:
	case BYTECODE_OP_LT_S64:
	case BYTECODE_OP_GE_S64:
	case BYTECODE_OP_LE_S64:
	case BYTECODE_OP_BIT_RSHIFT:
	case BYTECODE_OP_BIT_LSHIFT:
	case BYTECODE_OP_BIT_AND:
	case BYTECODE_OP_BIT_OR:
	case BYTECODE_OP_BIT_XOR:
	{
		next_pc += sizeof(struct binary_op);
		break;
	}

	/* unary */
	case BYTECODE_OP_UNARY_PLUS:
	case BYTECODE_OP_UNARY_MINUS:
	case BYTECODE_OP_UNARY_PLUS_S64:
	case BYTECODE_OP_UNARY_MINUS_S64:
	case BYTECODE_OP_UNARY_NOT_S64:
	case BYTECODE_OP_UNARY_NOT:
	case BYTECODE_OP_UNARY_BIT_NOT:
	{
		next_pc += sizeof(struct unary_op);
		break;
	}

	/* logical */
	case BYTECODE_OP_AND:
	case BYTECODE_OP_OR:
	{
		next_pc += sizeof(struct logical_op);
		break;
	}

	/* load field ref */
	case BYTECODE_OP_LOAD_FIELD_REF:
	/* get context ref */
	case BYTECODE_OP_GET_CONTEXT_REF:
	{
		next_pc += sizeof(struct load_op) + sizeof(struct field_ref);
		break;
	}
	case BYTECODE_OP_LOAD_FIELD_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_SEQUENCE:
	case BYTECODE_OP_GET_CONTEXT_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_REF_S64:
	case BYTECODE_OP_GET_CONTEXT_REF_S64:
	{
		/*
		 * Reject specialized load field ref instructions.
		 */
		ret = -EINVAL;
		goto end;
	}

	/* load from immediate operand */
	case BYTECODE_OP_LOAD_STRING:
	case BYTECODE_OP_LOAD_STAR_GLOB_STRING:
	{
		struct load_op *insn = (struct load_op *) pc;

		next_pc += sizeof(struct load_op) + strlen(insn->data) + 1;
		break;
	}

	case BYTECODE_OP_LOAD_S64:
	{
		next_pc += sizeof(struct load_op) + sizeof(struct literal_numeric);
		break;
	}

	case BYTECODE_OP_CAST_TO_S64:
	case BYTECODE_OP_CAST_NOP:
	{
		next_pc += sizeof(struct cast_op);
		break;
	}

	/*
	 * Instructions for recursive traversal through composed types.
	 */
	case BYTECODE_OP_GET_CONTEXT_ROOT:
	case BYTECODE_OP_GET_APP_CONTEXT_ROOT:
	case BYTECODE_OP_GET_PAYLOAD_ROOT:
	case BYTECODE_OP_LOAD_FIELD:
	{
		next_pc += sizeof(struct load_op);
		break;
	}

	case BYTECODE_OP_LOAD_FIELD_S8:
	case BYTECODE_OP_LOAD_FIELD_S16:
	case BYTECODE_OP_LOAD_FIELD_S32:
	case BYTECODE_OP_LOAD_FIELD_S64:
	case BYTECODE_OP_LOAD_FIELD_U8:
	case BYTECODE_OP_LOAD_FIELD_U16:
	case BYTECODE_OP_LOAD_FIELD_U32:
	case BYTECODE_OP_LOAD_FIELD_U64:
	case BYTECODE_OP_LOAD_FIELD_STRING:
	case BYTECODE_OP_LOAD_FIELD_SEQUENCE:
	case BYTECODE_OP_LOAD_FIELD_DOUBLE:
	{
		/*
		 * Reject specialized load field instructions.
		 */
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_GET_SYMBOL:
	case BYTECODE_OP_GET_SYMBOL_FIELD:
	{
		next_pc += sizeof(struct load_op) + sizeof(struct get_symbol);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U16:
	{
		next_pc += sizeof(struct load_op) + sizeof(struct get_index_u16);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U64:
	{
		next_pc += sizeof(struct load_op) + sizeof(struct get_index_u64);
		break;
	}

	}
end:
	*_next_pc = next_pc;
	return ret;
}

/*
 * Return value:
 * >0: going to next insn.
 * 0: success, stop iteration.
 * <0: error
 */
static
int exec_insn(struct bytecode_runtime *bytecode,
		struct mp_table *mp_table,
		struct vstack *stack,
		char **_next_pc,
		char *pc)
{
	int ret = 1;
	char *next_pc = *_next_pc;

	switch (*(bytecode_opcode_t *) pc) {
	case BYTECODE_OP_UNKNOWN:
	default:
	{
		printk(KERN_WARNING "LTTng: bytecode: unknown bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_RETURN:
	{
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_DOUBLE:
		case REG_STRING:
		case REG_PTR:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d at end of bytecode\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		ret = 0;
		goto end;
	}

	case BYTECODE_OP_RETURN_S64:
	{
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
			break;
		default:
		case REG_TYPE_UNKNOWN:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d at end of bytecode\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		ret = 0;
		goto end;
	}

	/* binary */
	case BYTECODE_OP_MUL:
	case BYTECODE_OP_DIV:
	case BYTECODE_OP_MOD:
	case BYTECODE_OP_PLUS:
	case BYTECODE_OP_MINUS:
	/* Floating point */
	case BYTECODE_OP_EQ_DOUBLE:
	case BYTECODE_OP_NE_DOUBLE:
	case BYTECODE_OP_GT_DOUBLE:
	case BYTECODE_OP_LT_DOUBLE:
	case BYTECODE_OP_GE_DOUBLE:
	case BYTECODE_OP_LE_DOUBLE:
	case BYTECODE_OP_EQ_DOUBLE_S64:
	case BYTECODE_OP_NE_DOUBLE_S64:
	case BYTECODE_OP_GT_DOUBLE_S64:
	case BYTECODE_OP_LT_DOUBLE_S64:
	case BYTECODE_OP_GE_DOUBLE_S64:
	case BYTECODE_OP_LE_DOUBLE_S64:
	case BYTECODE_OP_EQ_S64_DOUBLE:
	case BYTECODE_OP_NE_S64_DOUBLE:
	case BYTECODE_OP_GT_S64_DOUBLE:
	case BYTECODE_OP_LT_S64_DOUBLE:
	case BYTECODE_OP_GE_S64_DOUBLE:
	case BYTECODE_OP_LE_S64_DOUBLE:
	case BYTECODE_OP_UNARY_PLUS_DOUBLE:
	case BYTECODE_OP_UNARY_MINUS_DOUBLE:
	case BYTECODE_OP_UNARY_NOT_DOUBLE:
	case BYTECODE_OP_LOAD_FIELD_REF_DOUBLE:
	case BYTECODE_OP_GET_CONTEXT_REF_DOUBLE:
	case BYTECODE_OP_LOAD_DOUBLE:
	case BYTECODE_OP_CAST_DOUBLE_TO_S64:
	{
		printk(KERN_WARNING "LTTng: bytecode: unsupported bytecode op %u\n",
			(unsigned int) *(bytecode_opcode_t *) pc);
		ret = -EINVAL;
		goto end;
	}

	case BYTECODE_OP_EQ:
	case BYTECODE_OP_NE:
	case BYTECODE_OP_GT:
	case BYTECODE_OP_LT:
	case BYTECODE_OP_GE:
	case BYTECODE_OP_LE:
	case BYTECODE_OP_EQ_STRING:
	case BYTECODE_OP_NE_STRING:
	case BYTECODE_OP_GT_STRING:
	case BYTECODE_OP_LT_STRING:
	case BYTECODE_OP_GE_STRING:
	case BYTECODE_OP_LE_STRING:
	case BYTECODE_OP_EQ_STAR_GLOB_STRING:
	case BYTECODE_OP_NE_STAR_GLOB_STRING:
	case BYTECODE_OP_EQ_S64:
	case BYTECODE_OP_NE_S64:
	case BYTECODE_OP_GT_S64:
	case BYTECODE_OP_LT_S64:
	case BYTECODE_OP_GE_S64:
	case BYTECODE_OP_LE_S64:
	{
		/* Pop 2, push 1 */
		if (vstack_pop(stack)) {
			ret = -EINVAL;
			goto end;
		}
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_DOUBLE:
		case REG_STRING:
		case REG_STAR_GLOB_STRING:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		vstack_ax(stack)->type = REG_S64;
		next_pc += sizeof(struct binary_op);
		break;
	}
	case BYTECODE_OP_BIT_RSHIFT:
	case BYTECODE_OP_BIT_LSHIFT:
	case BYTECODE_OP_BIT_AND:
	case BYTECODE_OP_BIT_OR:
	case BYTECODE_OP_BIT_XOR:
	{
		/* Pop 2, push 1 */
		if (vstack_pop(stack)) {
			ret = -EINVAL;
			goto end;
		}
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_DOUBLE:
		case REG_STRING:
		case REG_STAR_GLOB_STRING:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		vstack_ax(stack)->type = REG_U64;
		next_pc += sizeof(struct binary_op);
		break;
	}

	/* unary */
	case BYTECODE_OP_UNARY_PLUS:
	case BYTECODE_OP_UNARY_MINUS:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		vstack_ax(stack)->type = REG_TYPE_UNKNOWN;
		next_pc += sizeof(struct unary_op);
		break;
	}

	case BYTECODE_OP_UNARY_PLUS_S64:
	case BYTECODE_OP_UNARY_MINUS_S64:
	case BYTECODE_OP_UNARY_NOT_S64:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		next_pc += sizeof(struct unary_op);
		break;
	}

	case BYTECODE_OP_UNARY_NOT:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		next_pc += sizeof(struct unary_op);
		break;
	}

	case BYTECODE_OP_UNARY_BIT_NOT:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_TYPE_UNKNOWN:
			break;
		case REG_DOUBLE:
		default:
			printk(KERN_WARNING "LTTng: bytecode: Unexpected register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		vstack_ax(stack)->type = REG_U64;
		next_pc += sizeof(struct unary_op);
		break;
	}

	/* logical */
	case BYTECODE_OP_AND:
	case BYTECODE_OP_OR:
	{
		struct logical_op *insn = (struct logical_op *) pc;
		int merge_ret;

		/* Add merge point to table */
		merge_ret = merge_point_add_check(mp_table,
					insn->skip_offset, stack);
		if (merge_ret) {
			ret = merge_ret;
			goto end;
		}

		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		/* There is always a cast-to-s64 operation before a or/and op. */
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Incorrect register type %d for operation\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}

		/* Continue to next instruction */
		/* Pop 1 when jump not taken */
		if (vstack_pop(stack)) {
			ret = -EINVAL;
			goto end;
		}
		next_pc += sizeof(struct logical_op);
		break;
	}

	/* load field ref */
	case BYTECODE_OP_LOAD_FIELD_REF:
	{
		printk(KERN_WARNING "LTTng: bytecode: Unknown field ref type\n");
		ret = -EINVAL;
		goto end;
	}
	/* get context ref */
	case BYTECODE_OP_GET_CONTEXT_REF:
	{
		printk(KERN_WARNING "LTTng: bytecode: Unknown get context ref type\n");
		ret = -EINVAL;
		goto end;
	}
	case BYTECODE_OP_LOAD_FIELD_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_SEQUENCE:
	case BYTECODE_OP_GET_CONTEXT_REF_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_STRING:
	case BYTECODE_OP_LOAD_FIELD_REF_USER_SEQUENCE:
	{
		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_STRING;
		next_pc += sizeof(struct load_op) + sizeof(struct field_ref);
		break;
	}
	case BYTECODE_OP_LOAD_FIELD_REF_S64:
	case BYTECODE_OP_GET_CONTEXT_REF_S64:
	{
		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_S64;
		next_pc += sizeof(struct load_op) + sizeof(struct field_ref);
		break;
	}

	/* load from immediate operand */
	case BYTECODE_OP_LOAD_STRING:
	{
		struct load_op *insn = (struct load_op *) pc;

		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_STRING;
		next_pc += sizeof(struct load_op) + strlen(insn->data) + 1;
		break;
	}

	case BYTECODE_OP_LOAD_STAR_GLOB_STRING:
	{
		struct load_op *insn = (struct load_op *) pc;

		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_STAR_GLOB_STRING;
		next_pc += sizeof(struct load_op) + strlen(insn->data) + 1;
		break;
	}

	case BYTECODE_OP_LOAD_S64:
	{
		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_S64;
		next_pc += sizeof(struct load_op)
				+ sizeof(struct literal_numeric);
		break;
	}

	case BYTECODE_OP_CAST_TO_S64:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n");
			ret = -EINVAL;
			goto end;
		}
		switch (vstack_ax(stack)->type) {
		case REG_S64:
		case REG_U64:
		case REG_DOUBLE:
		case REG_TYPE_UNKNOWN:
			break;
		default:
			printk(KERN_WARNING "LTTng: bytecode: Incorrect register type %d for cast\n",
				(int) vstack_ax(stack)->type);
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_S64;
		next_pc += sizeof(struct cast_op);
		break;
	}
	case BYTECODE_OP_CAST_NOP:
	{
		next_pc += sizeof(struct cast_op);
		break;
	}

	/*
	 * Instructions for recursive traversal through composed types.
	 */
	case BYTECODE_OP_GET_CONTEXT_ROOT:
	case BYTECODE_OP_GET_APP_CONTEXT_ROOT:
	case BYTECODE_OP_GET_PAYLOAD_ROOT:
	{
		if (vstack_push(stack)) {
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_PTR;
		next_pc += sizeof(struct load_op);
		break;
	}

	case BYTECODE_OP_LOAD_FIELD:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_TYPE_UNKNOWN;
		next_pc += sizeof(struct load_op);
		break;
	}

	case BYTECODE_OP_LOAD_FIELD_S8:
	case BYTECODE_OP_LOAD_FIELD_S16:
	case BYTECODE_OP_LOAD_FIELD_S32:
	case BYTECODE_OP_LOAD_FIELD_S64:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_S64;
		next_pc += sizeof(struct load_op);
		break;
	}
	case BYTECODE_OP_LOAD_FIELD_U8:
	case BYTECODE_OP_LOAD_FIELD_U16:
	case BYTECODE_OP_LOAD_FIELD_U32:
	case BYTECODE_OP_LOAD_FIELD_U64:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_U64;
		next_pc += sizeof(struct load_op);
		break;
	}
	case BYTECODE_OP_LOAD_FIELD_STRING:
	case BYTECODE_OP_LOAD_FIELD_SEQUENCE:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_STRING;
		next_pc += sizeof(struct load_op);
		break;
	}

	case BYTECODE_OP_LOAD_FIELD_DOUBLE:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		vstack_ax(stack)->type = REG_DOUBLE;
		next_pc += sizeof(struct load_op);
		break;
	}

	case BYTECODE_OP_GET_SYMBOL:
	case BYTECODE_OP_GET_SYMBOL_FIELD:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		next_pc += sizeof(struct load_op) + sizeof(struct get_symbol);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U16:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		next_pc += sizeof(struct load_op) + sizeof(struct get_index_u16);
		break;
	}

	case BYTECODE_OP_GET_INDEX_U64:
	{
		/* Pop 1, push 1 */
		if (!vstack_ax(stack)) {
			printk(KERN_WARNING "LTTng: bytecode: Empty stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		if (vstack_ax(stack)->type != REG_PTR) {
			printk(KERN_WARNING "LTTng: bytecode: Expecting pointer on top of stack\n\n");
			ret = -EINVAL;
			goto end;
		}
		next_pc += sizeof(struct load_op) + sizeof(struct get_index_u64);
		break;
	}

	}
end:
	*_next_pc = next_pc;
	return ret;
}

int lttng_bytecode_validate_load(struct bytecode_runtime *bytecode)
{
	char *pc, *next_pc, *start_pc;
	int ret = -EINVAL;

	start_pc = &bytecode->code[0];
	for (pc = next_pc = start_pc; pc - start_pc < bytecode->len;
			pc = next_pc) {
		ret = bytecode_validate_overflow(bytecode, start_pc, pc);
		if (ret != 0) {
			if (ret == -ERANGE)
				printk(KERN_WARNING "LTTng: bytecode: bytecode overflow\n");
			goto end;
		}
		dbg_printk("Validating loads: op %s (%u)\n",
			lttng_bytecode_print_op((unsigned int) *(bytecode_opcode_t *) pc),
			(unsigned int) *(bytecode_opcode_t *) pc);

		ret = validate_load(&next_pc, pc);
		if (ret)
			goto end;
	}
end:
	return ret;
}

/*
 * Never called concurrently (hash seed is shared).
 */
int lttng_bytecode_validate(struct bytecode_runtime *bytecode)
{
	struct mp_table *mp_table;
	char *pc, *next_pc, *start_pc;
	int ret = -EINVAL;
	struct vstack stack;

	vstack_init(&stack);

	mp_table = kzalloc(sizeof(*mp_table), GFP_KERNEL);
	if (!mp_table) {
		printk(KERN_WARNING "LTTng: bytecode: Error allocating hash table for bytecode validation\n");
		return -ENOMEM;
	}
	start_pc = &bytecode->code[0];
	for (pc = next_pc = start_pc; pc - start_pc < bytecode->len;
			pc = next_pc) {
		ret = bytecode_validate_overflow(bytecode, start_pc, pc);
		if (ret != 0) {
			if (ret == -ERANGE)
				printk(KERN_WARNING "LTTng: bytecode: bytecode overflow\n");
			goto end;
		}
		dbg_printk("Validating op %s (%u)\n",
			lttng_bytecode_print_op((unsigned int) *(bytecode_opcode_t *) pc),
			(unsigned int) *(bytecode_opcode_t *) pc);

		/*
		 * For each instruction, validate the current context
		 * (traversal of entire execution flow), and validate
		 * all merge points targeting this instruction.
		 */
		ret = validate_instruction_all_contexts(bytecode, mp_table,
					&stack, start_pc, pc);
		if (ret)
			goto end;
		ret = exec_insn(bytecode, mp_table, &stack, &next_pc, pc);
		if (ret <= 0)
			goto end;
	}
end:
	if (delete_all_nodes(mp_table)) {
		if (!ret) {
			printk(KERN_WARNING "LTTng: bytecode: Unexpected merge points\n");
			ret = -EINVAL;
		}
	}
	kfree(mp_table);
	return ret;
}
