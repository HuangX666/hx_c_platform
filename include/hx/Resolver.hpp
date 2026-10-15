#ifndef HX_C_PLATFORM_CPP_RESOLVER_HPP
#define HX_C_PLATFORM_CPP_RESOLVER_HPP

#include <hx_os_resolver.h>
#include <hx/Status.hpp>

namespace hx {
class Addresses {
public:
	explicit Addresses(hx_os_address_list *list = NULL) noexcept
	    : m_list_(list)
	{
	}

	Addresses(Addresses &&other) noexcept
	    : m_list_(other.m_list_)
	{
		other.m_list_ = NULL;
	}

	Addresses &operator=(Addresses &&other) noexcept
	{
		if (this != &other) {
			hx_os_address_list_destroy(m_list_);
			m_list_ = other.m_list_;
			other.m_list_ = NULL;
		}

		return *this;
	}

	Addresses(const Addresses &) = delete;
	Addresses &operator=(const Addresses &) = delete;

	~Addresses() noexcept
	{
		hx_os_address_list_destroy(m_list_);
	}

	size_t size() const noexcept
	{
		return hx_os_address_list_count(m_list_);
	}

	const hx_os_address *get(size_t index) const noexcept
	{
		return hx_os_address_list_get(m_list_, index);
	}

private:
	hx_os_address_list *m_list_;
};

/**
 * \brief Explicit owner; calls drive DNS on this thread without hidden workers.
 */
class Resolver {
public:
	explicit Resolver(hx_os_resolver *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> create(const hx_os_resolver_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_resolver_create(
			context, options, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<Addresses> resolve(const char *host, uint16_t port,
		int protocol = HX_OS_TCP,
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000),
		hx_os_cancel_token *cancel = NULL) const noexcept
	{
		hx_os_address_list *list = NULL;
		hx_os_error error = {};
		auto status = hx_os_resolver_resolve_until(*m_owner_, host,
			port, protocol, deadline, cancel, &list, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return Addresses(list);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_resolver_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_resolver *get() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_resolver **m_owner_;
};
} // namespace hx
#endif
