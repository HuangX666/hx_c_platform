#ifndef HX_C_PLATFORM_CPP_NETWORK_INTERFACES_HPP
#define HX_C_PLATFORM_CPP_NETWORK_INTERFACES_HPP

#include <hx_os_net_interfaces.h>
#include <hx_os_net_extra.h>
#include <hx/Values.hpp>

namespace hx {

class NetworkInterfaces {
public:
	NetworkInterfaces() noexcept
	    : m_list_(NULL)
	{
	}

	static Result<NetworkInterfaces> query(
		hx_os_context *context = NULL) noexcept
	{
		NetworkInterfaces result;
		hx_os_error error = {};
		auto status = hx_os_interfaces_query(
			context, &result.m_list_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<hx_os_route_info> routeTo(hx_os_network *network,
		const hx_os_address &destination) noexcept
	{
		hx_os_route_info result = {};
		hx_os_error error = {};
		auto status = hx_os_route_query(
			network, &destination, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	NetworkInterfaces(const NetworkInterfaces &) = delete;
	NetworkInterfaces &operator=(const NetworkInterfaces &) = delete;

	NetworkInterfaces(NetworkInterfaces &&other) noexcept
	    : m_list_(other.m_list_)
	{
		other.m_list_ = NULL;
	}

	NetworkInterfaces &operator=(NetworkInterfaces &&other) noexcept
	{
		if (this != &other) {
			hx_os_interface_list_destroy(m_list_);
			m_list_ = other.m_list_;
			other.m_list_ = NULL;
		}

		return *this;
	}

	~NetworkInterfaces() noexcept
	{
		hx_os_interface_list_destroy(m_list_);
	}

	size_t size() const noexcept
	{
		return hx_os_interface_list_count(m_list_);
	}

	const hx_os_interface_info *get(size_t index) const noexcept
	{
		return hx_os_interface_list_get(m_list_, index);
	}

private:
	hx_os_interface_list *m_list_;
};

} // namespace hx
#endif
