#include "regex_private.h"

typedef struct hx_os_rx_width {
	uint32_t minimum, maximum;
} hx_os_rx_width;

typedef struct hx_os_rx_compiler {
	hx_os_regex *regex;
	hx_os_error *error;
	hx_os_rx_width widths[HX_OS_REGEX_MAX_CAPTURES + 1];
	hx_os_status status;
	uint32_t depth;
	unsigned char width_states[HX_OS_REGEX_MAX_CAPTURES + 1];
} hx_os_rx_compiler;

static uint32_t hx_os_rx_add_width(uint32_t a, uint32_t b)
{
	return a > UINT32_MAX - b ? HX_OS_RX_INFINITY : a + b;
}

static uint32_t hx_os_rx_mul_width(uint32_t a, uint32_t b)
{
	if (!a || !b)
		return 0;
	return a > UINT32_MAX / b ? HX_OS_RX_INFINITY : a * b;
}

static hx_os_rx_width hx_os_rx_width_of(
	hx_os_rx_compiler *compiler, uint32_t index, unsigned depth)
{
	const hx_os_rx_node *node =
		(const hx_os_rx_node *)compiler->regex->nodes.data + index;
	hx_os_rx_width result = {0, 0}, left, right;

	if (compiler->status == HX_OS_OK)
		compiler->status = hx_os_i_regex_compile_work(
			compiler->regex, 1, node->offset, compiler->error);
	if (compiler->status != HX_OS_OK)
		return result;
	if (depth > compiler->regex->options.max_nesting * 4 + 64) {
		result.maximum = HX_OS_RX_INFINITY;
		return result;
	}
	switch (node->type) {
	case HX_OS_RX_CHAR:
	case HX_OS_RX_DOT:
	case HX_OS_RX_CLASS:
		result.minimum = result.maximum = 1;
		break;
	case HX_OS_RX_NEWLINE:
		result.minimum = 1;
		result.maximum = 2;
		break;
	case HX_OS_RX_GRAPHEME:
		result.minimum = 1;
		result.maximum = HX_OS_RX_INFINITY;
		break;
	case HX_OS_RX_CONCAT:
	case HX_OS_RX_ALT:
	case HX_OS_RX_CONDITION:
		left = hx_os_rx_width_of(compiler, node->left, depth + 1);
		right = hx_os_rx_width_of(compiler, node->right, depth + 1);
		if (node->type == HX_OS_RX_CONCAT) {
			result.minimum =
				hx_os_rx_add_width(left.minimum, right.minimum);
			result.maximum =
				hx_os_rx_add_width(left.maximum, right.maximum);
		}
		else {
			result.minimum = left.minimum < right.minimum
				? left.minimum
				: right.minimum;
			result.maximum = left.maximum > right.maximum
				? left.maximum
				: right.maximum;
		}
		break;
	case HX_OS_RX_REPEAT:
		if (!node->maximum)
			break;
		left = hx_os_rx_width_of(compiler, node->left, depth + 1);
		result.minimum =
			hx_os_rx_mul_width(left.minimum, node->minimum);
		result.maximum =
			hx_os_rx_mul_width(left.maximum, node->maximum);
		break;
	case HX_OS_RX_GROUP:
	case HX_OS_RX_ATOMIC:
		result = hx_os_rx_width_of(compiler, node->left, depth + 1);
		break;
	case HX_OS_RX_CALL:
	case HX_OS_RX_BACKREF:
		if (!node->value || compiler->width_states[node->value] == 1) {
			result.maximum = HX_OS_RX_INFINITY;
			break;
		}
		if (compiler->width_states[node->value] == 2) {
			result = compiler->widths[node->value];
			break;
		}
		/* A visiting group is recursive; finished widths are reusable.
		 */
		compiler->width_states[node->value] = 1;
		result = hx_os_rx_width_of(compiler,
			compiler->regex->groups[node->value].node, depth + 1);
		compiler->widths[node->value] = result;
		compiler->width_states[node->value] = 2;
		break;
	default:
		break;
	}
	return result;
}

static uint32_t hx_os_rx_emit(hx_os_rx_compiler *compiler, uint32_t type,
	uint32_t x, uint32_t y, uint32_t value, uint32_t flags)
{
	hx_os_rx_instruction instruction = {type, x, y, value, flags, 0, 0};
	uint32_t index =
		(uint32_t)(compiler->regex->code.size / sizeof(instruction));

	if (compiler->status != HX_OS_OK)
		return HX_OS_RX_NONE;
	compiler->status = hx_os_i_regex_compile_work(
		compiler->regex, 1, 0, compiler->error);
	if (compiler->status == HX_OS_OK)
		compiler->status = hx_os_i_regex_append(compiler->regex,
			&compiler->regex->code,
			(hx_os_bytes_view){&instruction, sizeof(instruction)},
			0, compiler->error);
	return compiler->status == HX_OS_OK ? index : HX_OS_RX_NONE;
}

static hx_os_rx_instruction *hx_os_rx_instruction_at(
	hx_os_rx_compiler *compiler, uint32_t index)
{
	return (hx_os_rx_instruction *)compiler->regex->code.data + index;
}

static uint32_t hx_os_rx_compile_node(
	hx_os_rx_compiler *compiler, uint32_t index, uint32_t next);

static uint32_t hx_os_rx_compile_repeat(
	hx_os_rx_compiler *compiler, const hx_os_rx_node *node, uint32_t next)
{
	uint32_t entry = next, i;
	hx_os_rx_node body_node =
		((hx_os_rx_node *)compiler->regex->nodes.data)[node->left];

	if (body_node.type == HX_OS_RX_CHAR || body_node.type == HX_OS_RX_DOT ||
		body_node.type == HX_OS_RX_CLASS) {
		entry = hx_os_rx_emit(compiler, HX_OS_RX_REPEAT_SIMPLE, next,
			body_node.type, body_node.value,
			body_node.flags | (node->flags & HX_OS_RX_LAZY));
		if (entry != HX_OS_RX_NONE) {
			hx_os_rx_instruction_at(compiler, entry)->minimum =
				node->minimum;
			hx_os_rx_instruction_at(compiler, entry)->maximum =
				node->maximum;
		}
		return entry;
	}

	if (node->maximum == HX_OS_RX_INFINITY) {
		uint32_t split, guard, body, slot;

		if (compiler->regex->slots == HX_OS_RX_SLOTS) {
			compiler->status = hx_os_i_regex_error(compiler->error,
				HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, 0);
			return HX_OS_RX_NONE;
		}
		slot = compiler->regex->slots++;
		split = hx_os_rx_emit(compiler, HX_OS_RX_SPLIT, 0, 0, 0, 0);
		guard = hx_os_rx_emit(
			compiler, HX_OS_RX_GUARD, split, next, slot, 0);
		body = hx_os_rx_compile_node(compiler, node->left, guard);
		body = hx_os_rx_emit(compiler, HX_OS_RX_SAVE, body, 0, slot, 0);
		if (compiler->status != HX_OS_OK)
			return HX_OS_RX_NONE;
		hx_os_rx_instruction_at(compiler, split)->x =
			node->flags & HX_OS_RX_LAZY ? next : body;
		hx_os_rx_instruction_at(compiler, split)->y =
			node->flags & HX_OS_RX_LAZY ? body : next;
		entry = split;
	}
	else {
		for (i = node->maximum;
			i > node->minimum && compiler->status == HX_OS_OK;
			--i) {
			uint32_t body = hx_os_rx_compile_node(
				compiler, node->left, entry);

			entry = hx_os_rx_emit(compiler, HX_OS_RX_SPLIT,
				node->flags & HX_OS_RX_LAZY ? entry : body,
				node->flags & HX_OS_RX_LAZY ? body : entry, 0,
				0);
		}
	}
	for (i = 0; i < node->minimum && compiler->status == HX_OS_OK; ++i)
		entry = hx_os_rx_compile_node(compiler, node->left, entry);
	return entry;
}

static uint32_t hx_os_rx_compile_node(
	hx_os_rx_compiler *compiler, uint32_t index, uint32_t next)
{
	hx_os_rx_node node;
	uint32_t left, right, entry = HX_OS_RX_NONE;

	if (compiler->status != HX_OS_OK || index == HX_OS_RX_NONE)
		return HX_OS_RX_NONE;
	compiler->status = hx_os_i_regex_compile_work(
		compiler->regex, 1, 0, compiler->error);
	if (compiler->status != HX_OS_OK)
		return HX_OS_RX_NONE;
	if (++compiler->depth > compiler->regex->options.max_nesting * 4 + 64) {
		compiler->status = hx_os_i_regex_error(
			compiler->error, HX_OS_LIMIT, HX_OS_RX_DEPTH_LIMIT, 0);
		--compiler->depth;
		return HX_OS_RX_NONE;
	}
	node = ((hx_os_rx_node *)compiler->regex->nodes.data)[index];
	switch (node.type) {
	case HX_OS_RX_EMPTY:
		entry = next;
		break;
	case HX_OS_RX_CONCAT:
		right = hx_os_rx_compile_node(compiler, node.right, next);
		entry = hx_os_rx_compile_node(compiler, node.left, right);
		break;
	case HX_OS_RX_ALT:
		left = hx_os_rx_compile_node(compiler, node.left, next);
		right = hx_os_rx_compile_node(compiler, node.right, next);
		entry = hx_os_rx_emit(
			compiler, HX_OS_RX_SPLIT, left, right, 0, 1);
		break;
	case HX_OS_RX_REPEAT:
		entry = hx_os_rx_compile_repeat(compiler, &node, next);
		break;
	case HX_OS_RX_GROUP:
		right = hx_os_rx_emit(
			compiler, HX_OS_RX_GROUP_END, next, 0, node.value, 0);
		left = hx_os_rx_compile_node(compiler, node.left, right);
		entry = hx_os_rx_emit(
			compiler, HX_OS_RX_SAVE, left, 0, node.value, 0);
		if (compiler->regex->groups[node.value].entry == HX_OS_RX_NONE)
			compiler->regex->groups[node.value].entry = entry;
		break;
	case HX_OS_RX_ASSERT: {
		hx_os_rx_width width = {0, 0};
		uint32_t failed = HX_OS_RX_NONE, success = next;

		if (node.right != HX_OS_RX_NONE) {
			hx_os_rx_node branches =
				((hx_os_rx_node *)compiler->regex->nodes
						.data)[node.right];

			success = hx_os_rx_compile_node(
				compiler, branches.left, next);
			failed = hx_os_rx_compile_node(
				compiler, branches.right, next);
		}
		if (node.flags & HX_OS_RX_BEHIND) {
			width = hx_os_rx_width_of(compiler, node.left, 0);
			if (compiler->status != HX_OS_OK)
				break;
			if (width.maximum == HX_OS_RX_INFINITY ||
				width.maximum > compiler->regex->options
							.max_lookbehind) {
				compiler->status = hx_os_i_regex_error(
					compiler->error, HX_OS_SYNTAX,
					HX_OS_RX_BEHIND_LIMIT, 0);
				break;
			}
		}
		right = hx_os_rx_emit(
			compiler, HX_OS_RX_ASSERT_END, 0, 0, 0, 0);
		left = hx_os_rx_compile_node(compiler, node.left, right);
		entry = hx_os_rx_emit(compiler, HX_OS_RX_ASSERT, left, success,
			failed, node.flags);
		if (entry != HX_OS_RX_NONE) {
			hx_os_rx_instruction_at(compiler, entry)->minimum =
				width.minimum;
			hx_os_rx_instruction_at(compiler, entry)->maximum =
				width.maximum;
		}
		break;
	}
	case HX_OS_RX_ATOMIC:
		right = hx_os_rx_emit(
			compiler, HX_OS_RX_ATOMIC_END, 0, 0, 0, 0);
		left = hx_os_rx_compile_node(compiler, node.left, right);
		entry = hx_os_rx_emit(
			compiler, HX_OS_RX_ATOMIC, left, next, 0, 0);
		break;
	case HX_OS_RX_CONDITION:
		left = hx_os_rx_compile_node(compiler, node.left, next);
		right = hx_os_rx_compile_node(compiler, node.right, next);
		entry = hx_os_rx_emit(compiler, HX_OS_RX_CONDITION, left, right,
			node.value, node.flags);
		break;
	case HX_OS_RX_MARK:
		entry = hx_os_rx_emit(compiler, node.type, next, 0,
			compiler->regex->marks[node.value].slot, node.flags);
		break;
	case HX_OS_RX_SKIP:
		entry = hx_os_rx_emit(compiler, node.type, next, 0,
			node.value == HX_OS_RX_NONE
				? HX_OS_RX_NONE
				: compiler->regex->marks[node.value].slot,
			node.flags);
		break;
	default:
		entry = hx_os_rx_emit(
			compiler, node.type, next, 0, node.value, node.flags);
		break;
	}
	if (node.value != HX_OS_RX_NONE &&
		(node.type == HX_OS_RX_PRUNE || node.type == HX_OS_RX_THEN ||
			node.type == HX_OS_RX_COMMIT ||
			node.type == HX_OS_RX_ACCEPT ||
			node.type == HX_OS_RX_FAIL))
		entry = hx_os_rx_emit(compiler, HX_OS_RX_MARK, entry, 0,
			compiler->regex->marks[node.value].slot, node.flags);
	--compiler->depth;
	return entry;
}

hx_os_status hx_os_i_regex_compile(
	hx_os_regex *regex, uint32_t root, hx_os_error *error)
{
	hx_os_rx_compiler compiler;
	uint32_t end, body, group;

	memset(&compiler, 0, sizeof(compiler));
	compiler.regex = regex;
	compiler.error = error;
	regex->slots = regex->group_count + 1;
	for (group = 0; group <= regex->group_count; ++group)
		regex->groups[group].entry = HX_OS_RX_NONE;
	for (group = 0; group < regex->mark_count; ++group)
		regex->marks[group].slot = regex->slots++;
	end = hx_os_rx_emit(&compiler, HX_OS_RX_MATCH, 0, 0, 0, 0);
	body = hx_os_rx_compile_node(&compiler, root, end);
	regex->entry = hx_os_rx_emit(&compiler, HX_OS_RX_SAVE, body, 0, 0, 0);
	regex->groups[0].entry = regex->entry;
	regex->groups[0].node = root;
	for (group = 1;
		group <= regex->group_count && compiler.status == HX_OS_OK;
		++group)
		if (regex->groups[group].entry == HX_OS_RX_NONE)
			(void)hx_os_rx_compile_node(
				&compiler, regex->groups[group].node, end);
	return compiler.status;
}
