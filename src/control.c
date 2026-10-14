#include "internal.h"
#include "hx_os_control.h"

#define HX_OS_CONTROL_SECOND UINT64_C(1000000000)

hx_os_status hx_os_stopwatch_start(hx_os_stopwatch *watch, hx_os_error *error)
{
	uint64_t now;
	hx_os_status status;

	if (!watch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_high_resolution_read(&now, error);
	if (status != HX_OS_OK)
		return status;

	watch->started_ns = now;
	watch->started = 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_stopwatch_elapsed_ns(const hx_os_stopwatch *watch,
	uint64_t *elapsed, hx_os_error *error)
{
	uint64_t now;
	hx_os_status status;

	if (!watch || !watch->started || !elapsed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_high_resolution_read(&now, error);
	if (status != HX_OS_OK)
		return status;
	if (now < watch->started_ns)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*elapsed = now - watch->started_ns;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_stopwatch_restart(
	hx_os_stopwatch *watch, uint64_t *elapsed, hx_os_error *error)
{
	uint64_t amount;
	hx_os_status status;

	if (!elapsed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_stopwatch_elapsed_ns(watch, &amount, error);
	if (status != HX_OS_OK)
		return status;

	watch->started_ns += amount;
	*elapsed = amount;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_backoff_init(hx_os_backoff *backoff,
	const hx_os_backoff_options *options, hx_os_error *error)
{
	const hx_os_backoff_options defaults = HX_OS_BACKOFF_OPTIONS_INIT;
	hx_os_backoff_options selected = options ? *options : defaults;

	if (!backoff || !selected.initial_ms ||
		selected.max_ms < selected.initial_ms ||
		selected.jitter_percent > 100)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	backoff->options = selected;
	backoff->nominal_ms = selected.initial_ms;
	backoff->random_state = selected.seed ? selected.seed : 1;
	backoff->used = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static uint64_t hx_os_backoff_random(uint64_t *state)
{
	uint64_t value = *state;

	value ^= value >> 12;
	value ^= value << 25;
	value ^= value >> 27;
	*state = value;

	return value * UINT64_C(2685821657736338717);
}

hx_os_status hx_os_backoff_next(hx_os_backoff *backoff, uint64_t *delay_ms,
	hx_os_error *error)
{
	uint64_t nominal, jitter, low, high, width, result;

	if (!backoff || !delay_ms || !backoff->options.initial_ms)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (backoff->used >= backoff->options.max_retries)
		return hx_os_i_error(error, HX_OS_EOF, 0, 0);

	nominal = backoff->nominal_ms;
	jitter = (nominal / 100) * backoff->options.jitter_percent +
		(nominal % 100) * backoff->options.jitter_percent / 100;
	low = nominal - jitter;
	high = jitter > backoff->options.max_ms - nominal
		? backoff->options.max_ms
		: nominal + jitter;
	width = high - low;
	result = low;
	if (width) {
		uint64_t random = hx_os_backoff_random(&backoff->random_state);

		result += width == UINT64_MAX ? random : random % (width + 1);
	}

	backoff->nominal_ms = nominal > backoff->options.max_ms / 2
		? backoff->options.max_ms
		: nominal * 2;
	++backoff->used;
	*delay_ms = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_backoff_reset(hx_os_backoff *backoff, hx_os_error *error)
{
	if (!backoff)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_backoff_init(backoff, &backoff->options, error);
}

hx_os_status hx_os_token_bucket_init(hx_os_token_bucket *bucket,
	uint64_t capacity, uint32_t rate_per_second, hx_os_error *error)
{
	uint64_t now;
	hx_os_status status;

	if (!bucket || !capacity || !rate_per_second)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;
	if (now == UINT64_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	bucket->capacity = capacity;
	bucket->tokens = capacity;
	bucket->updated_ns = now;
	bucket->fraction = 0;
	bucket->rate_per_second = rate_per_second;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_token_bucket_refill(hx_os_token_bucket *bucket, uint64_t now)
{
	uint64_t elapsed = now - bucket->updated_ns;
	uint64_t seconds = elapsed / HX_OS_CONTROL_SECOND;
	uint64_t space = bucket->capacity - bucket->tokens;
	uint64_t whole, fraction, subsecond;

	bucket->updated_ns = now;
	if (seconds > space / bucket->rate_per_second) {
		bucket->tokens = bucket->capacity;
		bucket->fraction = 0;

		return;
	}

	whole = seconds * bucket->rate_per_second;
	fraction = (elapsed % HX_OS_CONTROL_SECOND) *
		bucket->rate_per_second + bucket->fraction;
	subsecond = fraction / HX_OS_CONTROL_SECOND;
	if (subsecond >= space - whole) {
		bucket->tokens = bucket->capacity;
		bucket->fraction = 0;
	}
	else {
		bucket->tokens += whole + subsecond;
		bucket->fraction = (uint32_t)(fraction % HX_OS_CONTROL_SECOND);
	}
}

static hx_os_deadline hx_os_token_bucket_retry(
	const hx_os_token_bucket *bucket, uint64_t needed, uint64_t now)
{
	uint64_t seconds = needed / bucket->rate_per_second;
	uint64_t part = (needed % bucket->rate_per_second) *
		HX_OS_CONTROL_SECOND;
	uint64_t nanoseconds, available = UINT64_MAX - 1 - now;

	// Borrow a second when fractional credit exceeds the remainder.
	if (part < bucket->fraction) {
		--seconds;
		part += (uint64_t)bucket->rate_per_second *
			HX_OS_CONTROL_SECOND;
	}
	part -= bucket->fraction;
	nanoseconds = part / bucket->rate_per_second +
		(part % bucket->rate_per_second != 0);
	if (seconds > available / HX_OS_CONTROL_SECOND)
		return UINT64_MAX - 1;

	seconds *= HX_OS_CONTROL_SECOND;
	if (nanoseconds > available - seconds)
		return UINT64_MAX - 1;

	return now + seconds + nanoseconds;
}

hx_os_status hx_os_token_bucket_try_take(hx_os_token_bucket *bucket,
	uint64_t amount, hx_os_deadline *retry_at, hx_os_error *error)
{
	hx_os_token_bucket next;
	uint64_t now;
	hx_os_status status;

	if (!bucket || !retry_at || !bucket->capacity ||
		!bucket->rate_per_second)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (amount > bucket->capacity)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (!amount) {
		*retry_at = HX_OS_DEADLINE_NOW;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;
	if (now == UINT64_MAX || now < bucket->updated_ns)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	next = *bucket;
	hx_os_token_bucket_refill(&next, now);
	if (amount <= next.tokens) {
		next.tokens -= amount;
		*retry_at = HX_OS_DEADLINE_NOW;
		status = HX_OS_OK;
	}
	else {
		*retry_at = hx_os_token_bucket_retry(
			&next, amount - next.tokens, now);
		status = HX_OS_WOULD_BLOCK;
	}

	*bucket = next;

	return hx_os_i_error(error, status, 0, 0);
}
