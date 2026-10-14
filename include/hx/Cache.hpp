#ifndef HX_C_PLATFORM_CPP_CACHE_HPP
#define HX_C_PLATFORM_CPP_CACHE_HPP

#include <hx_cache.h>
#include <hx/Values.hpp>
#include <type_traits>

namespace hx {
template <typename T> class LruCache {
	static_assert(std::is_trivially_copyable<T>::value,
		"Use the C copy/drop API for resource-owning values");

public:
	LruCache() noexcept
	    : m_cache_(NULL)
	{
	}

	LruCache(LruCache &&other) noexcept
	    : m_cache_(other.m_cache_)
	{
		other.m_cache_ = NULL;
	}

	LruCache &operator=(LruCache &&other) noexcept
	{
		std::swap(m_cache_, other.m_cache_);

		return *this;
	}

	LruCache(const LruCache &) = delete;
	LruCache &operator=(const LruCache &) = delete;

	~LruCache() noexcept
	{
		(void)hx_os_cache_destroy(&m_cache_, NULL);
	}

	static Result<LruCache> create(size_t maxEntries = 256,
		size_t maxBytes = 1024 * 1024,
		hx_os_context *context = NULL) noexcept
	{
		LruCache cache;
		hx_os_container_options options;
		hx_os_error error = {};

		hx_os_container_options_init(&options, sizeof(T));
		options.element_alignment = alignof(T);
		options.max_items = maxEntries;
		options.max_bytes = maxBytes;

		auto status = hx_os_cache_create(
			context, &options, &cache.m_cache_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return cache;
	}

	Result<void> put(
		const void *key, size_t keyBytes, const T &value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cache_put(
			m_cache_, key, keyBytes, &value, &error);

		return detail::Status(status, error);
	}

	Result<void> put(StringView key, const T &value) noexcept
	{
		return put(key.data(), key.size(), value);
	}

	Result<void> putFor(const void *key, size_t keyBytes, const T &value,
		uint64_t ttlMs) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cache_put_for(
			m_cache_, key, keyBytes, &value, ttlMs, &error);

		return detail::Status(status, error);
	}

	Result<void> putFor(
		StringView key, const T &value, uint64_t ttlMs) noexcept
	{
		return putFor(key.data(), key.size(), value, ttlMs);
	}

	Result<void> putUntil(const void *key, size_t keyBytes, const T &value,
		hx_os_deadline expires) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cache_put_until(
			m_cache_, key, keyBytes, &value, expires, &error);

		return detail::Status(status, error);
	}

	Result<void> putUntil(
		StringView key, const T &value, hx_os_deadline expires) noexcept
	{
		return putUntil(key.data(), key.size(), value, expires);
	}

	Result<const T *> get(const void *key, size_t keyBytes) noexcept
	{
		const void *value = NULL;
		hx_os_error error = {};
		auto status = hx_os_cache_get(
			m_cache_, key, keyBytes, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return static_cast<const T *>(value);
	}

	Result<const T *> get(StringView key) noexcept
	{
		return get(key.data(), key.size());
	}

	Result<const T *> peek(const void *key, size_t keyBytes) const noexcept
	{
		const void *value = NULL;
		hx_os_error error = {};
		auto status = hx_os_cache_peek(
			m_cache_, key, keyBytes, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return static_cast<const T *>(value);
	}

	Result<const T *> peek(StringView key) const noexcept
	{
		return peek(key.data(), key.size());
	}

	Result<void> erase(const void *key, size_t keyBytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cache_erase(
			m_cache_, key, keyBytes, NULL, &error);

		return detail::Status(status, error);
	}

	Result<void> erase(StringView key) noexcept
	{
		return erase(key.data(), key.size());
	}

	size_t size() const noexcept
	{
		return hx_os_cache_size(m_cache_);
	}

	size_t memory() const noexcept
	{
		return hx_os_cache_memory(m_cache_);
	}

	Result<hx_os_cache_stats> stats() const noexcept
	{
		hx_os_cache_stats result = {};
		auto status = hx_os_cache_get_stats(m_cache_, &result);
		if (status != HX_OS_OK)
			return Failure(status);

		return result;
	}

	Result<size_t> purgeExpired() noexcept
	{
		size_t removed = 0;
		hx_os_error error = {};
		auto status =
			hx_os_cache_purge_expired(m_cache_, &removed, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return removed;
	}

	Result<void> clear() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_cache_clear(m_cache_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_cache *m_cache_;
};
} // namespace hx
#endif
