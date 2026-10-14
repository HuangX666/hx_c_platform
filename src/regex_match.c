#include "regex_private.h"
#if HX_OS_HAVE_UNICODE
#include <utf8proc.h>
#endif

typedef struct hx_os_rx_capture {
	size_t start, end;
} hx_os_rx_capture;

typedef struct hx_os_rx_call {
	size_t position, mark, marker_end, next_start;
	hx_os_rx_capture saved;
	uint32_t type, resume, failed, group, flags, committed;
} hx_os_rx_call;

typedef struct hx_os_rx_frame {
	size_t position, next_start, minimum, maximum;
	uint32_t pc, depth, kind, committed, lazy;
} hx_os_rx_frame;

enum {
	HX_OS_RX_FRAME_BRANCH,
	HX_OS_RX_FRAME_ALT,
	HX_OS_RX_FRAME_FAIL,
	HX_OS_RX_FRAME_PASS,
	HX_OS_RX_FRAME_REPEAT
};

typedef struct hx_os_rx_state {
	const hx_os_regex *regex;
	hx_string_view subject;
	hx_os_rx_budget *budget;
	hx_os_error *error;
	hx_os_rx_capture *captures;
	hx_os_rx_call *calls;
	hx_os_rx_capture *saved;
	hx_bytes stack;
	size_t position, origin, next_start;
	uint32_t pc, depth, committed;
	hx_os_status status;
} hx_os_rx_state;

uint32_t hx_os_i_regex_fold(uint32_t point)
{
#if HX_OS_HAVE_UNICODE
	utf8proc_int32_t folded[4], lower_folded[4];
	utf8proc_ssize_t count = utf8proc_decompose_char(
		(utf8proc_int32_t)point, folded, 4, UTF8PROC_CASEFOLD, NULL);
	utf8proc_int32_t lower;
	utf8proc_ssize_t lower_count;

	if (count == 1)
		return (uint32_t)folded[0];
	// Unicode 17 simple folds not exposed by full fold or lowercase.
	if (point == 0xfb05)
		return 0xfb06;
	if (point == 0x1fd3)
		return 0x390;
	if (point == 0x1fe3)
		return 0x3b0;
	lower = utf8proc_tolower((utf8proc_int32_t)point);
	if ((uint32_t)lower == point || count <= 0 || count > 4)
		return point;
	lower_count = utf8proc_decompose_char(
		lower, lower_folded, 4, UTF8PROC_CASEFOLD, NULL);
	// Lowercasing alone would incorrectly merge dotted I with ASCII i.
	return lower_count == count &&
			!memcmp(folded, lower_folded,
				(size_t)count * sizeof(*folded))
		? (uint32_t)lower
		: point;
#else
	return point >= 'A' && point <= 'Z' ? point + 32 : point;
#endif
}

static int hx_os_rx_category(uint32_t point)
{
#if HX_OS_HAVE_UNICODE
	return (int)utf8proc_category((utf8proc_int32_t)point);
#else
	(void)point;
	return 0;
#endif
}

int hx_os_i_regex_property(uint32_t point, uint32_t property, int unicode)
{
	int category;
	int letter, digit;

	if (property & HX_OS_RX_UNICODE_PROPERTY) {
		unicode = 1;
		property &= ~HX_OS_RX_UNICODE_PROPERTY;
	}
	category = unicode ? hx_os_rx_category(point) : 0;
	letter = category >= 1 && category <= 5;
	digit = category == 9;
	int ascii_letter = (point >= 'A' && point <= 'Z') ||
		(point >= 'a' && point <= 'z');
	int ascii_digit = point >= '0' && point <= '9';

	switch (property) {
	case HX_OS_RX_DIGIT:
		return unicode ? digit : ascii_digit;
	case HX_OS_RX_WORD:
		return unicode ? letter || category == 6 ||
				(category >= 9 && category <= 12)
			       : ascii_letter || ascii_digit || point == '_';
	case HX_OS_RX_SPACE:
		return point == ' ' || (point >= 9 && point <= 13) ||
			(unicode &&
				(point == 0x85 ||
					(category >= 23 && category <= 25)));
	case HX_OS_RX_HSPACE:
	case HX_OS_RX_BLANK:
		return point == 9 || point == 32 || point == 0xa0 ||
			point == 0x1680 || point == 0x180e ||
			(point >= 0x2000 && point <= 0x200a) ||
			point == 0x202f || point == 0x205f || point == 0x3000;
	case HX_OS_RX_VSPACE:
		return (point >= 10 && point <= 13) || point == 0x85 ||
			point == 0x2028 || point == 0x2029;
	case HX_OS_RX_ALPHA:
		return unicode ? letter : ascii_letter;
	case HX_OS_RX_ALNUM:
		return unicode ? letter || (category >= 9 && category <= 11)
			       : ascii_letter || ascii_digit;
	case HX_OS_RX_ASCII:
		return point < 128;
	case HX_OS_RX_ANY:
		return point <= 0x10ffff;
	case HX_OS_RX_CASED:
		return hx_os_rx_category(point) >= 1 &&
			hx_os_rx_category(point) <= 3;
	case HX_OS_RX_LOWER:
		return unicode ? category == 2 : point >= 'a' && point <= 'z';
	case HX_OS_RX_UPPER:
		return unicode ? category == 1 : point >= 'A' && point <= 'Z';
	case HX_OS_RX_XDIGIT:
		return ascii_digit || (point >= 'a' && point <= 'f') ||
			(point >= 'A' && point <= 'F');
	case HX_OS_RX_CNTRL:
		return unicode ? category == 26 : point < 32 || point == 127;
	case HX_OS_RX_GRAPH:
		return unicode ? category >= 1 && category <= 22
			       : point >= 33 && point <= 126;
	case HX_OS_RX_PRINT:
		return unicode ? category >= 1 && category <= 23
			       : point >= 32 && point <= 126;
	case HX_OS_RX_PUNCT:
		return unicode ? category >= 12 && category <= 18
			       : point >= 33 && point <= 126 && !ascii_letter &&
				!ascii_digit;
	default:
		if (property >= HX_OS_RX_CATEGORY &&
			property < HX_OS_RX_CATEGORY + 31)
			return hx_os_rx_category(point) ==
				(int)(property - HX_OS_RX_CATEGORY == 30
						? 0
						: property - HX_OS_RX_CATEGORY);
		switch (property - HX_OS_RX_CATEGORY - 64) {
		case 0:
			return hx_os_rx_category(point) >= 1 &&
				hx_os_rx_category(point) <= 5;
		case 1:
			return hx_os_rx_category(point) >= 6 &&
				hx_os_rx_category(point) <= 8;
		case 2:
			return hx_os_rx_category(point) >= 9 &&
				hx_os_rx_category(point) <= 11;
		case 3:
			return hx_os_rx_category(point) >= 12 &&
				hx_os_rx_category(point) <= 18;
		case 4:
			return hx_os_rx_category(point) >= 19 &&
				hx_os_rx_category(point) <= 22;
		case 5:
			return hx_os_rx_category(point) >= 23 &&
				hx_os_rx_category(point) <= 25;
		case 6:
			return hx_os_rx_category(point) == 0 ||
				hx_os_rx_category(point) >= 26;
		default:
			return 0;
		}
	}
}

static uint32_t hx_os_rx_case_property(uint32_t property, uint32_t flags)
{
	uint32_t plain = property & ~HX_OS_RX_UNICODE_PROPERTY;

	if (!(flags & HX_OS_REGEX_CASELESS))
		return property;
	if ((plain == HX_OS_RX_LOWER || plain == HX_OS_RX_UPPER) &&
		!(flags & HX_OS_REGEX_UCP) &&
		!(property & HX_OS_RX_UNICODE_PROPERTY))
		return HX_OS_RX_ALPHA;
	if (plain >= HX_OS_RX_CATEGORY + 1 && plain <= HX_OS_RX_CATEGORY + 3)
		return HX_OS_RX_CASED | HX_OS_RX_UNICODE_PROPERTY;
	return property;
}

static size_t hx_os_rx_previous(hx_string_view input, size_t position)
{
	if (!position)
		return 0;
	--position;
	while (position && ((unsigned char)input.data[position] & 0xc0) == 0x80)
		--position;
	return position;
}

static uint32_t hx_os_rx_decode(
	hx_string_view input, size_t position, size_t *next)
{
	uint32_t point = 0;
	size_t bytes = 0;

	if (position < input.size)
		(void)hx_os_i_utf_decode(HX_OS_UTF8,
			(const unsigned char *)input.data + position,
			input.size - position, &point, &bytes);
	*next = position + bytes;
	return point;
}

static int hx_os_rx_character(const hx_os_regex *regex, uint32_t type,
	uint32_t value, uint32_t flags, uint32_t point)
{
	if (type == HX_OS_RX_CHAR)
		return point == value ||
			(flags & HX_OS_REGEX_CASELESS &&
				hx_os_i_regex_fold(point) ==
					hx_os_i_regex_fold(value));
	if (type == HX_OS_RX_DOT)
		return point != '\n' || flags & HX_OS_REGEX_DOTALL;
	if (type == HX_OS_RX_CLASS) {
		const hx_os_rx_class *character_class =
			(const hx_os_rx_class *)regex->classes.data + value;
		const hx_os_rx_range *ranges =
			(const hx_os_rx_range *)regex->ranges.data +
			character_class->first;
		uint32_t i;
		uint32_t folded = flags & HX_OS_REGEX_CASELESS
			? hx_os_i_regex_fold(point)
			: point;
		int hit = 0;

		for (i = 0; i < character_class->count; ++i) {
			int current;

			if (ranges[i].property) {
				uint32_t property = hx_os_rx_case_property(
					ranges[i].property, flags);

				current =
					hx_os_i_regex_property(point, property,
						!!(flags & HX_OS_REGEX_UCP)) !=
					(int)ranges[i].negate;
			}
			else
				current = (point >= ranges[i].first &&
						  point <= ranges[i].last) ||
					(folded >= ranges[i].first &&
						folded <= ranges[i].last);
			if (current) {
				hit = 1;
				break;
			}
		}
		return hit != (int)character_class->negate;
	}
	return 0;
}

static int hx_os_rx_push(hx_os_rx_state *state, uint32_t pc, size_t position,
	uint32_t kind, size_t minimum, size_t maximum, int lazy)
{
	hx_os_rx_frame frame;
	size_t offset = state->stack.size;
	size_t captures_bytes = state->regex->slots * sizeof(*state->captures);
	size_t calls_bytes = state->depth * sizeof(*state->calls);
	size_t saved_bytes = state->depth * captures_bytes;
	size_t bytes = sizeof(frame) + captures_bytes + calls_bytes +
		saved_bytes + sizeof(offset);
	size_t capacity;

	if (bytes > state->budget->limits.max_stack_bytes - state->stack.size) {
		state->status = hx_os_i_regex_error(state->error, HX_OS_LIMIT,
			HX_OS_RX_MEMORY_LIMIT, state->position);
		return 0;
	}
	capacity = state->stack.size + bytes;
	// Reserve growth is capped by the same retained stack budget.
	if (capacity > state->stack.capacity) {
		size_t grown = state->stack.capacity ? state->stack.capacity * 2
						     : 1024;
		unsigned char *memory;

		if (grown < capacity)
			grown = capacity;
		if (grown > state->budget->limits.max_stack_bytes)
			grown = state->budget->limits.max_stack_bytes;
		memory = hx_os_i_alloc(state->regex->context, grown, 0);
		if (!memory) {
			state->status = hx_os_i_error(
				state->error, HX_OS_NO_MEMORY, 0, 0);
			return 0;
		}
		if (state->stack.size)
			memcpy(memory, state->stack.data, state->stack.size);
		hx_os_memory_free(state->stack.data);
		state->stack.data = memory;
		state->stack.capacity = grown;
	}
	frame.position = position;
	frame.pc = pc;
	frame.depth = state->depth;
	frame.kind = kind;
	frame.next_start = state->next_start;
	frame.committed = state->committed;
	frame.minimum = minimum;
	frame.maximum = maximum;
	frame.lazy = (uint32_t)lazy;
	memcpy(state->stack.data + offset, &frame, sizeof(frame));
	offset += sizeof(frame);
	memcpy(state->stack.data + offset, state->captures, captures_bytes);
	offset += captures_bytes;
	if (calls_bytes)
		memcpy(state->stack.data + offset, state->calls, calls_bytes);
	offset += calls_bytes;
	if (saved_bytes)
		memcpy(state->stack.data + offset, state->saved, saved_bytes);
	offset += saved_bytes;
	memcpy(state->stack.data + offset, &state->stack.size, sizeof(offset));
	state->stack.size = capacity;
	return 1;
}

static void hx_os_rx_pop(hx_os_rx_state *state, hx_os_rx_frame *frame)
{
	size_t offset,
		captures_bytes = state->regex->slots * sizeof(*state->captures);

	memcpy(&offset, state->stack.data + state->stack.size - sizeof(offset),
		sizeof(offset));
	state->stack.size = offset;
	memcpy(frame, state->stack.data + offset, sizeof(*frame));
	offset += sizeof(*frame);
	memcpy(state->captures, state->stack.data + offset, captures_bytes);
	offset += captures_bytes;
	if (frame->depth)
		memcpy(state->calls, state->stack.data + offset,
			frame->depth * sizeof(*state->calls));
	offset += frame->depth * sizeof(*state->calls);
	if (frame->depth)
		memcpy(state->saved, state->stack.data + offset,
			frame->depth * captures_bytes);
	state->pc = frame->pc;
	state->position = frame->position;
	state->depth = frame->depth;
	state->next_start = frame->next_start;
	state->committed = frame->committed;
}

static int hx_os_rx_backtrack(hx_os_rx_state *state)
{
	while (state->stack.size) {
		hx_os_rx_frame frame;

		state->status =
			hx_os_i_regex_step(state->budget, 1, state->error);
		if (state->status != HX_OS_OK)
			return 0;
		hx_os_rx_pop(state, &frame);
		if (frame.kind == HX_OS_RX_FRAME_FAIL)
			continue;
		if (frame.kind == HX_OS_RX_FRAME_REPEAT) {
			size_t next;

			if (frame.lazy)
				(void)hx_os_rx_decode(
					state->subject, frame.position, &next);
			else
				next = hx_os_rx_previous(
					state->subject, frame.position);
			if (frame.lazy ? frame.position < frame.maximum
				       : frame.position > frame.minimum)
				if (!hx_os_rx_push(state, frame.pc, next,
					    HX_OS_RX_FRAME_REPEAT,
					    frame.minimum, frame.maximum,
					    (int)frame.lazy))
					return 0;
		}
		return 1;
	}
	return 0;
}

static int hx_os_rx_scope(hx_os_rx_state *state,
	const hx_os_rx_instruction *instruction, int atomic)
{
	hx_os_rx_call call;
	size_t positions[1025], count = 0, current = state->position, i;
	int negative = !atomic && !!(instruction->flags & HX_OS_RX_NEGATE);

	if (state->depth == state->budget->limits.max_depth) {
		state->status = hx_os_i_regex_error(state->error, HX_OS_LIMIT,
			HX_OS_RX_DEPTH_LIMIT, state->position);
		return 0;
	}
	memset(&call, 0, sizeof(call));
	call.position = state->position;
	call.next_start = state->next_start;
	call.committed = state->committed;
	call.mark = state->stack.size;
	call.type = atomic ? HX_OS_RX_ATOMIC : HX_OS_RX_ASSERT;
	call.resume = instruction->y;
	call.failed = atomic ? HX_OS_RX_NONE : instruction->value;
	call.flags = instruction->flags;
	call.saved = state->captures[0];
	memcpy(state->saved + state->depth * state->regex->slots,
		state->captures,
		state->regex->slots * sizeof(*state->captures));
	if (!hx_os_rx_push(state, negative ? call.resume : call.failed,
		    state->position,
		    negative || call.failed != HX_OS_RX_NONE
			    ? HX_OS_RX_FRAME_PASS
			    : HX_OS_RX_FRAME_FAIL,
		    0, 0, 0))
		return 0;
	call.marker_end = state->stack.size;
	state->calls[state->depth++] = call;
	if (!atomic && instruction->flags & HX_OS_RX_BEHIND) {
		for (i = 0; i <= instruction->maximum; ++i) {
			if (i >= instruction->minimum)
				positions[count++] = current;
			if (!current)
				break;
			current = hx_os_rx_previous(state->subject, current);
		}
		if (!count)
			return hx_os_rx_backtrack(state);
		// Farthest admissible beginning is attempted first.
		for (i = 0; i + 1 < count; ++i)
			if (!hx_os_rx_push(state, instruction->x, positions[i],
				    HX_OS_RX_FRAME_BRANCH, 0, 0, 0))
				return 0;
		state->position = positions[count - 1];
	}
	state->pc = instruction->x;
	return 1;
}

static int hx_os_rx_scope_end(hx_os_rx_state *state, int atomic)
{
	hx_os_rx_call call = state->calls[state->depth - 1];

	if (!atomic && call.flags & HX_OS_RX_BEHIND &&
		state->position != call.position)
		return hx_os_rx_backtrack(state);
	if (!atomic && call.flags & HX_OS_RX_NEGATE) {
		hx_os_rx_frame restored;

		state->stack.size = call.marker_end;
		hx_os_rx_pop(state, &restored);
		if (call.failed != HX_OS_RX_NONE) {
			state->pc = call.failed;
			return 1;
		}
		return hx_os_rx_backtrack(state);
	}
	state->stack.size = call.mark;
	--state->depth;
	if (!atomic) {
		state->position = call.position;
		state->next_start = call.next_start;
		state->committed = call.committed;
	}
	if (!atomic)
		state->captures[0] = call.saved;
	state->pc = call.resume;
	return 1;
}

static int hx_os_rx_repeat_simple(
	hx_os_rx_state *state, const hx_os_rx_instruction *instruction)
{
	size_t current = state->position, minimum = current, maximum;
	uint32_t count = 0;
	int lazy = !!(instruction->flags & HX_OS_RX_LAZY);

	while (current < state->subject.size && count < instruction->maximum) {
		size_t next;
		uint32_t point =
			hx_os_rx_decode(state->subject, current, &next);

		if (!hx_os_rx_character(state->regex, instruction->y,
			    instruction->value, instruction->flags, point))
			break;
		current = next;
		++count;
		if (count == instruction->minimum)
			minimum = current;
		state->status =
			hx_os_i_regex_step(state->budget, 1, state->error);
		if (state->status != HX_OS_OK)
			return 0;
	}
	if (count < instruction->minimum)
		return hx_os_rx_backtrack(state);
	maximum = current;
	if (minimum < maximum) {
		size_t alternative;

		if (lazy)
			(void)hx_os_rx_decode(
				state->subject, minimum, &alternative);
		else
			alternative =
				hx_os_rx_previous(state->subject, maximum);
		if (!hx_os_rx_push(state, instruction->x, alternative,
			    HX_OS_RX_FRAME_REPEAT, minimum, maximum, lazy))
			return 0;
	}
	state->position = lazy ? minimum : maximum;
	state->pc = instruction->x;
	return 1;
}

static int hx_os_rx_anchor(
	hx_os_rx_state *state, const hx_os_rx_instruction *instruction)
{
	size_t position = state->position, end = state->subject.size;
	const char *text = state->subject.data;

	switch (instruction->value) {
	case 'A':
		return !position;
	case 'G':
		return position == state->origin;
	case 'z':
		return position == end;
	case 'Z':
		return position == end ||
			(position + 1 == end && text[position] == '\n');
	case '^':
		return !position ||
			(instruction->flags & HX_OS_REGEX_MULTILINE &&
				text[position - 1] == '\n');
	case '$':
		return position == end ||
			(position < end && text[position] == '\n' &&
				(instruction->flags & HX_OS_REGEX_MULTILINE ||
					position + 1 == end));
	default:
		return 0;
	}
}

static int hx_os_rx_backreference(
	hx_os_rx_state *state, const hx_os_rx_instruction *instruction)
{
	hx_os_rx_capture capture = state->captures[instruction->value];
	size_t left = capture.start, right = state->position;

	if (left == SIZE_MAX || capture.end == SIZE_MAX)
		return 0;
	if (!(instruction->flags & HX_OS_REGEX_CASELESS)) {
		size_t bytes = capture.end - left;

		state->status = hx_os_i_regex_step(
			state->budget, (uint64_t)bytes + 1, state->error);
		if (state->status != HX_OS_OK)
			return 0;
		if (bytes > state->subject.size - right ||
			(bytes &&
				memcmp(state->subject.data + left,
					state->subject.data + right, bytes)))
			return 0;
		state->position += bytes;
		return 1;
	}
	while (left < capture.end) {
		uint32_t a, b;

		if (right == state->subject.size)
			return 0;
		a = hx_os_rx_decode(state->subject, left, &left);
		b = hx_os_rx_decode(state->subject, right, &right);
		if (hx_os_i_regex_fold(a) != hx_os_i_regex_fold(b))
			return 0;
		state->status =
			hx_os_i_regex_step(state->budget, 1, state->error);
		if (state->status != HX_OS_OK)
			return 0;
	}
	state->position = right;
	return 1;
}

static int hx_os_rx_call_enter(
	hx_os_rx_state *state, const hx_os_rx_instruction *instruction)
{
	hx_os_rx_call call;

	if (state->depth == state->budget->limits.max_depth) {
		state->status = hx_os_i_regex_error(state->error, HX_OS_LIMIT,
			HX_OS_RX_DEPTH_LIMIT, state->position);
		return 0;
	}
	memset(&call, 0, sizeof(call));
	call.type = HX_OS_RX_CALL;
	call.resume = instruction->x;
	call.group = instruction->value;
	call.saved = state->captures[call.group];
	call.position = state->position;
	call.next_start = state->next_start;
	call.committed = state->committed;
	call.mark = state->stack.size;
	memcpy(state->saved + state->depth * state->regex->slots,
		state->captures,
		state->regex->slots * sizeof(*state->captures));
	state->calls[state->depth++] = call;
	state->pc = state->regex->groups[call.group].entry;
	return 1;
}

static void hx_os_rx_call_return(hx_os_rx_state *state)
{
	hx_os_rx_call call = state->calls[--state->depth];

	memcpy(state->captures,
		state->saved + state->depth * state->regex->slots,
		state->regex->slots * sizeof(*state->captures));
	state->pc = call.resume;
	state->next_start = call.next_start;
	state->committed = call.committed;
}

static void hx_os_rx_cut(hx_os_rx_state *state, int then)
{
	size_t floor = 0;
	uint32_t depth;

	for (depth = state->depth; depth; --depth)
		if (state->calls[depth - 1].type == HX_OS_RX_ASSERT ||
			state->calls[depth - 1].type == HX_OS_RX_CALL) {
			floor = state->calls[depth - 1].type == HX_OS_RX_CALL
				? state->calls[depth - 1].mark
				: state->calls[depth - 1].marker_end;
			break;
		}
	if (then) {
		size_t end = state->stack.size;

		while (end > floor) {
			size_t start;
			hx_os_rx_frame frame;

			memcpy(&start, state->stack.data + end - sizeof(start),
				sizeof(start));
			memcpy(&frame, state->stack.data + start,
				sizeof(frame));
			if (frame.kind == HX_OS_RX_FRAME_ALT) {
				state->stack.size = end;
				return;
			}
			end = start;
		}
	}
	state->stack.size = floor;
	for (depth = 0; depth < state->depth; ++depth)
		if (state->calls[depth].type == HX_OS_RX_ATOMIC &&
			state->calls[depth].mark > floor)
			state->calls[depth].mark = floor;
}

static int hx_os_rx_global_control(const hx_os_rx_state *state)
{
	uint32_t depth;

	for (depth = 0; depth < state->depth; ++depth)
		if (state->calls[depth].type != HX_OS_RX_ATOMIC)
			return 0;
	return 1;
}

static int hx_os_rx_recursing(const hx_os_rx_state *state, uint32_t group)
{
	uint32_t depth;

	for (depth = state->depth; depth; --depth) {
		const hx_os_rx_call *call = state->calls + depth - 1;

		if (call->type == HX_OS_RX_CALL)
			return group == UINT32_MAX - 1 || call->group == group;
	}
	return 0;
}

static void hx_os_rx_close_captures(hx_os_rx_state *state, uint32_t depth)
{
	hx_os_rx_capture *saved =
		depth ? state->saved + (depth - 1) * state->regex->slots : NULL;
	uint32_t group;

	for (group = 1; group <= state->regex->group_count; ++group) {
		hx_os_rx_capture *capture = state->captures + group;
		int opened = !saved || saved[group].start != capture->start ||
			saved[group].end != SIZE_MAX;

		if (capture->start != SIZE_MAX && capture->end == SIZE_MAX &&
			opened)
			capture->end = state->position;
	}
}

static hx_os_status hx_os_rx_execute(
	hx_os_rx_state *state, int full, hx_os_regex_result *out)
{
	const hx_os_rx_instruction *instructions =
		(const hx_os_rx_instruction *)state->regex->code.data;

	for (;;) {
		const hx_os_rx_instruction *instruction =
			instructions + state->pc;
		size_t next;
		uint32_t point;
		int ok = 1;

		state->status =
			hx_os_i_regex_step(state->budget, 1, state->error);
		if (state->status != HX_OS_OK)
			return state->status;
		switch (instruction->type) {
		case HX_OS_RX_CHAR:
		case HX_OS_RX_CLASS:
		case HX_OS_RX_DOT:
			point = hx_os_rx_decode(
				state->subject, state->position, &next);
			if (state->position == state->subject.size ||
				!hx_os_rx_character(state->regex,
					instruction->type, instruction->value,
					instruction->flags, point))
				ok = hx_os_rx_backtrack(state);
			else {
				state->position = next;
				state->pc = instruction->x;
			}
			break;
		case HX_OS_RX_ANCHOR:
			if (!hx_os_rx_anchor(state, instruction))
				ok = hx_os_rx_backtrack(state);
			else
				state->pc = instruction->x;
			break;
		case HX_OS_RX_BOUNDARY: {
			int before = 0, after = 0;

			if (state->position)
				before = hx_os_i_regex_property(
					hx_os_rx_decode(state->subject,
						hx_os_rx_previous(
							state->subject,
							state->position),
						&next),
					HX_OS_RX_WORD,
					!!(instruction->flags &
						HX_OS_REGEX_UCP));
			if (state->position < state->subject.size)
				after = hx_os_i_regex_property(
					hx_os_rx_decode(state->subject,
						state->position, &next),
					HX_OS_RX_WORD,
					!!(instruction->flags &
						HX_OS_REGEX_UCP));
			if ((before != after) == (instruction->value == 0))
				state->pc = instruction->x;
			else
				ok = hx_os_rx_backtrack(state);
			break;
		}
		case HX_OS_RX_SPLIT:
			ok = hx_os_rx_push(state, instruction->y,
				state->position,
				instruction->flags ? HX_OS_RX_FRAME_ALT
						   : HX_OS_RX_FRAME_BRANCH,
				0, 0, 0);
			state->pc = instruction->x;
			break;
		case HX_OS_RX_SAVE:
			state->captures[instruction->value].start =
				state->position;
			state->captures[instruction->value].end = SIZE_MAX;
			state->pc = instruction->x;
			break;
		case HX_OS_RX_GROUP_END:
			state->captures[instruction->value].end =
				state->position;
			if (state->depth &&
				state->calls[state->depth - 1].type ==
					HX_OS_RX_CALL &&
				state->calls[state->depth - 1].group ==
					instruction->value)
				hx_os_rx_call_return(state);
			else
				state->pc = instruction->x;
			break;
		case HX_OS_RX_GUARD:
			state->pc = state->captures[instruction->value].start ==
					state->position
				? instruction->y
				: instruction->x;
			break;
		case HX_OS_RX_REPEAT_SIMPLE:
			ok = hx_os_rx_repeat_simple(state, instruction);
			break;
		case HX_OS_RX_BACKREF:
			if (hx_os_rx_backreference(state, instruction))
				state->pc = instruction->x;
			else if (state->status == HX_OS_OK)
				ok = hx_os_rx_backtrack(state);
			break;
		case HX_OS_RX_ASSERT:
			ok = hx_os_rx_scope(state, instruction, 0);
			break;
		case HX_OS_RX_ATOMIC:
			ok = hx_os_rx_scope(state, instruction, 1);
			break;
		case HX_OS_RX_ASSERT_END:
			ok = hx_os_rx_scope_end(state, 0);
			break;
		case HX_OS_RX_ATOMIC_END:
			ok = hx_os_rx_scope_end(state, 1);
			break;
		case HX_OS_RX_CALL:
			ok = hx_os_rx_call_enter(state, instruction);
			break;
		case HX_OS_RX_CONDITION: {
			int condition;

			if (instruction->value == UINT32_MAX)
				condition = 0;
			else if (instruction->value == UINT32_MAX - 1 ||
				instruction->flags & HX_OS_RX_RECURSION)
				condition = hx_os_rx_recursing(
					state, instruction->value);
			else
				condition = state->captures[instruction->value]
						    .end != SIZE_MAX;
			state->pc = condition ? instruction->x : instruction->y;
			break;
		}
		case HX_OS_RX_NEWLINE:
			point = hx_os_rx_decode(
				state->subject, state->position, &next);
			if (state->position == state->subject.size ||
				!hx_os_i_regex_property(
					point, HX_OS_RX_VSPACE, 1))
				ok = hx_os_rx_backtrack(state);
			else {
				if (point == '\r' &&
					next < state->subject.size &&
					state->subject.data[next] == '\n')
					++next;
				state->position = next;
				state->pc = instruction->x;
			}
			break;
		case HX_OS_RX_GRAPHEME:
#if HX_OS_HAVE_UNICODE
			if (state->position == state->subject.size)
				ok = hx_os_rx_backtrack(state);
			else {
				utf8proc_int32_t grapheme_state = 0;

				point = hx_os_rx_decode(
					state->subject, state->position, &next);
				while (next < state->subject.size) {
					size_t after;
					uint32_t following = hx_os_rx_decode(
						state->subject, next, &after);

					if (utf8proc_grapheme_break_stateful(
						    (utf8proc_int32_t)point,
						    (utf8proc_int32_t)following,
						    &grapheme_state))
						break;
					point = following;
					next = after;
					state->status = hx_os_i_regex_step(
						state->budget, 1, state->error);
					if (state->status != HX_OS_OK)
						return state->status;
				}
				state->position = next;
				state->pc = instruction->x;
			}
#else
			return hx_os_i_error(
				state->error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
			break;
		case HX_OS_RX_RESET:
			state->captures[0].start = state->position;
			state->pc = instruction->x;
			break;
		case HX_OS_RX_FAIL:
			ok = hx_os_rx_backtrack(state);
			break;
		case HX_OS_RX_MARK:
			state->captures[instruction->value].start =
				state->position;
			state->pc = instruction->x;
			break;
		case HX_OS_RX_COMMIT:
			hx_os_rx_cut(state, 0);
			if (hx_os_rx_global_control(state))
				state->committed = 1;
			state->pc = instruction->x;
			break;
		case HX_OS_RX_PRUNE:
			hx_os_rx_cut(state, 0);
			state->pc = instruction->x;
			break;
		case HX_OS_RX_THEN:
			hx_os_rx_cut(state, 1);
			state->pc = instruction->x;
			break;
		case HX_OS_RX_SKIP:
			if (instruction->value == HX_OS_RX_NONE ||
				state->captures[instruction->value].start !=
					SIZE_MAX) {
				state->next_start =
					instruction->value == HX_OS_RX_NONE
					? state->position
					: state->captures[instruction->value]
						  .start;
				hx_os_rx_cut(state, 0);
			}
			state->pc = instruction->x;
			break;
		case HX_OS_RX_MATCH:
		case HX_OS_RX_ACCEPT: {
			uint32_t d;

			if (instruction->type == HX_OS_RX_MATCH &&
				state->depth &&
				state->calls[state->depth - 1].type ==
					HX_OS_RX_CALL &&
				!state->calls[state->depth - 1].group) {
				hx_os_rx_call_return(state);
				break;
			}
			if (instruction->type == HX_OS_RX_ACCEPT) {
				for (d = state->depth; d; --d)
					if (state->calls[d - 1].type ==
							HX_OS_RX_ASSERT ||
						state->calls[d - 1].type ==
							HX_OS_RX_CALL)
						break;
				if (d &&
					state->calls[d - 1].type ==
						HX_OS_RX_CALL) {
					state->stack.size =
						state->calls[d - 1].mark;
					state->depth = d;
					hx_os_rx_call_return(state);
					break;
				}
				hx_os_rx_close_captures(state, d);
				if (d) {
					state->depth = d;
					ok = hx_os_rx_scope_end(state, 0);
					break;
				}
			}
			if ((full && state->position != state->subject.size) ||
				(state->regex->notempty &&
					state->position ==
						state->captures[0].start &&
					(state->regex->notempty == 1 ||
						state->captures[0].start ==
							state->origin))) {
				ok = hx_os_rx_backtrack(state);
				break;
			}
			state->captures[0].end = state->position;
			out->count = state->regex->group_count + 1;
			for (d = 0; d < out->count; ++d) {
				out->captures[d].offset =
					state->captures[d].start;
				out->captures[d].size =
					state->captures[d].start == SIZE_MAX ||
						state->captures[d].end ==
							SIZE_MAX
					? 0
					: state->captures[d].end -
						state->captures[d].start;
				if (state->captures[d].end == SIZE_MAX && d)
					out->captures[d].offset = SIZE_MAX;
			}
			return hx_os_i_error(state->error, HX_OS_OK, 0, 0);
		}
		default:
			return hx_os_i_error(
				state->error, HX_OS_IO_ERROR, 0, 0);
		}
		if (state->status != HX_OS_OK)
			return state->status;
		if (!ok) {
			uint32_t depth = state->depth;

			while (depth--)
				if (state->calls[depth].type == HX_OS_RX_CALL) {
					state->next_start =
						state->calls[depth].next_start;
					state->committed =
						state->calls[depth].committed;
				}
			return HX_OS_NOT_FOUND;
		}
	}
}

hx_os_status hx_os_i_regex_run(const hx_os_regex *regex, hx_string_view subject,
	size_t start, int full, hx_os_rx_budget *budget,
	hx_os_regex_result *out, hx_os_error *error)
{
	hx_os_rx_state state;
	hx_os_regex_result result;
	size_t position = start, bad;
	hx_os_status status;

	if (!regex || !out || (!subject.data && subject.size) ||
		start > subject.size ||
		(start < subject.size &&
			((unsigned char)subject.data[start] & 0xc0) == 0x80))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (subject.size > budget->limits.max_subject_bytes)
		return hx_os_i_regex_error(
			error, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT, 0);
	if (!budget->validated || budget->validated_data != subject.data ||
		budget->validated_size != subject.size) {
		status = hx_os_text_validate_utf8(
			subject.data, subject.size, &bad);
		if (status != HX_OS_OK)
			return hx_os_i_regex_error(
				error, status, HX_OS_RX_BAD_ESCAPE, bad);
		budget->validated_data = subject.data;
		budget->validated_size = subject.size;
		budget->validated = 1;
	}
	if (regex->step_limit && regex->step_limit < budget->limits.max_steps)
		budget->limits.max_steps = regex->step_limit;
	status = hx_os_i_regex_step(budget, 0, error);
	if (status != HX_OS_OK)
		return status;
	memset(&state, 0, sizeof(state));
	memset(&result, 0, sizeof(result));
	state.regex = regex;
	state.subject = subject;
	state.origin = start;
	state.budget = budget;
	state.error = error;
	hx_bytes_init(&state.stack, regex->context);
	state.captures = hx_os_i_alloc(
		regex->context, regex->slots * sizeof(*state.captures), 0);
	state.calls = hx_os_i_alloc(regex->context,
		budget->limits.max_depth * sizeof(*state.calls), 0);
	state.saved = hx_os_i_zero(regex->context,
		budget->limits.max_depth * regex->slots * sizeof(*state.saved));
	if (!state.captures || !state.calls || !state.saved)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	else {
		for (;;) {
			memset(state.captures, 0xff,
				regex->slots * sizeof(*state.captures));
			state.position = position;
			state.next_start = position;
			state.pc = regex->entry;
			state.depth = 0;
			state.committed = 0;
			state.stack.size = 0;
			state.status = HX_OS_OK;
			status = hx_os_rx_execute(&state, full, &result);
			if (status != HX_OS_NOT_FOUND ||
				position == subject.size || full ||
				state.committed)
				break;
			if (state.next_start > position)
				position = state.next_start;
			else
				(void)hx_os_rx_decode(
					subject, position, &position);
		}
	}
	hx_os_memory_free(state.captures);
	hx_os_memory_free(state.calls);
	hx_os_memory_free(state.saved);
	hx_bytes_destroy(&state.stack);
	if (status == HX_OS_OK)
		*out = result;
	else if (status == HX_OS_NOT_FOUND)
		(void)hx_os_i_error(error, status, 0, 0);
	return status;
}
