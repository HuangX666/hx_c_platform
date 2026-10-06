#ifndef HX_C_PLATFORM_CPP_PROFILER_HPP
#define HX_C_PLATFORM_CPP_PROFILER_HPP

#include <hx_os_profiler.h>
#include <hx/Values.hpp>

namespace hx {

class ProfileSnapshot {
public:
	/* Adopts one snapshot reference; copying retains it without allocation.
	 */
	explicit ProfileSnapshot(hx_os_profiler_snapshot *value = NULL) noexcept
	    : m_value_(value)
	{
	}

	ProfileSnapshot(const ProfileSnapshot &other) noexcept
	    : m_value_(other.m_value_)
	{
		hx_os_profiler_snapshot_retain(m_value_);
	}

	ProfileSnapshot &operator=(const ProfileSnapshot &other) noexcept
	{
		hx_os_profiler_snapshot_retain(other.m_value_);
		hx_os_profiler_snapshot_release(m_value_);
		m_value_ = other.m_value_;

		return *this;
	}

	ProfileSnapshot(ProfileSnapshot &&other) noexcept
	    : m_value_(other.m_value_)
	{
		other.m_value_ = NULL;
	}

	ProfileSnapshot &operator=(ProfileSnapshot &&other) noexcept
	{
		if (this != &other) {
			hx_os_profiler_snapshot_release(m_value_);
			m_value_ = other.m_value_;
			other.m_value_ = NULL;
		}

		return *this;
	}

	~ProfileSnapshot() noexcept
	{
		hx_os_profiler_snapshot_release(m_value_);
	}

	size_t size() const noexcept
	{
		return hx_os_profiler_snapshot_count(m_value_);
	}

	uint64_t timeNs() const noexcept
	{
		return hx_os_profiler_snapshot_time_ns(m_value_);
	}

	uint32_t features() const noexcept
	{
		return hx_os_profiler_snapshot_features(m_value_);
	}

	const hx_os_profiler_entry *begin() const noexcept
	{
		return hx_os_profiler_snapshot_data(m_value_);
	}

	const hx_os_profiler_entry *end() const noexcept
	{
		return m_value_ ? begin() + size() : NULL;
	}

	const hx_os_profiler_entry *at(size_t index) const noexcept
	{
		return hx_os_profiler_snapshot_at(m_value_, index);
	}

	Result<hx_os_profiler_quantile> percentile(uint32_t recordId,
		unsigned percent, bool exclusive = false) const noexcept
	{
		hx_os_error error = {};
		hx_os_profiler_quantile value = {};
		auto status = hx_os_profiler_snapshot_quantile(m_value_,
			recordId, exclusive ? 1 : 0, percent, 100, &value,
			&error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	hx_os_profiler_snapshot *native() const noexcept
	{
		return m_value_;
	}

private:
	hx_os_profiler_snapshot *m_value_;
};

/* Host owns the profiler through all threads and TLS retirement. */
class Profiler {
public:
	explicit Profiler(hx_os_profiler *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> create(hx_os_context *context = NULL,
		const hx_os_profiler_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_profiler_create_with_options(
			context, options, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> startRoot(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_profiler_start_root(*m_owner_, name, &error);

		return detail::Status(status, error);
	}

	Result<void> createWithFeatures(uint32_t features,
		hx_os_context *context = NULL,
		const hx_os_profiler_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_profiler_create_with_features(
			context, options, features, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<ProfileSnapshot> snapshot(
		hx_os_context *context = NULL) const noexcept
	{
		hx_os_error error = {};
		hx_os_profiler_snapshot *value = NULL;
		auto status = hx_os_profiler_snapshot_create_with_context(
			context, *m_owner_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return ProfileSnapshot(value);
	}

	Result<void> reach(const char *name) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_profiler_reach(*m_owner_, name, &error);

		return detail::Status(status, error);
	}

	Result<void> leave() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_profiler_leave(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> restartThread() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_profiler_restart_thread(*m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<String> dump(hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_profiler_dump_string(
					*m_owner_, out, error);
			},
			context);
	}

	Result<void> save(const char *path) const noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_profiler_save(*m_owner_, path, &error);

		return detail::Status(status, error);
	}

	void stop() noexcept
	{
		hx_os_profiler_stop(*m_owner_);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_profiler_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	hx_os_profiler *native() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_profiler **m_owner_;
};

/* A lexical, nonmovable thread/fiber scope. start() reports entry failure;
 * only successful entries are balanced. The profiler outlives this object.
 * Do not interleave external restart/leave with an active guard.
 */
class ProfileScope {
public:
	explicit ProfileScope(hx_os_profiler *profiler) noexcept
	    : m_profiler_(profiler)
	    , m_active_(false)
	{
	}

	Result<void> start(const char *name, bool root = false) noexcept
	{
		if (m_active_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = root
			? hx_os_profiler_start_root(m_profiler_, name, &error)
			: hx_os_profiler_reach(m_profiler_, name, &error);

		if (status == HX_OS_OK)
			m_active_ = true;

		return detail::Status(status, error);
	}

	ProfileScope(const ProfileScope &) = delete;

	ProfileScope &operator=(const ProfileScope &) = delete;

	~ProfileScope() noexcept
	{
		if (m_active_ &&
			hx_os_profiler_leave(m_profiler_, NULL) != HX_OS_OK)
			(void)hx_os_profiler_restart_thread(m_profiler_, NULL);
	}

	Result<void> finish() noexcept
	{
		if (!m_active_)
			return Result<void>();

		hx_os_error error = {};

		auto status = hx_os_profiler_leave(m_profiler_, &error);
		if (status == HX_OS_OK)
			m_active_ = false;

		return detail::Status(status, error);
	}

private:
	hx_os_profiler *m_profiler_;
	bool m_active_;
};
} // namespace hx
#endif
