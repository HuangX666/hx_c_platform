#include <hx/Algorithms.hpp>
#include <cstdio>
#include <list>

struct Device {
	int id;
	int pending;
};

static bool device_less(const Device &left, const Device &right) noexcept
{
	return left.id < right.id;
}

int main()
{
	Device devices[] = {{23, 4}, {17, 0}, {31, 8}};

	hx::algo::sort(std::begin(devices), std::end(devices), device_less);
	std::puts("Devices sorted by id:");
	for (const auto &device : devices)
		std::printf(
			"  device %d: %d pending\n", device.id, device.pending);

	const Device key = {23, 0};
	auto found = hx::algo::lower_bound(
		std::begin(devices), std::end(devices), key, device_less);
	if (found != std::end(devices) && found->id == key.id)
		std::printf("Found device %d\n", found->id);

	Device busy[3];
	auto end = hx::algo::copy_if(std::begin(devices), std::end(devices),
		busy, [](const Device &device) noexcept {
			return device.pending != 0;
		});
	std::printf(
		"Devices needing work: %zu\n", static_cast<size_t>(end - busy));

	auto total = hx::algo::accumulate(
		busy, end, 0, [](int sum, const Device &device) noexcept {
			return sum + device.pending;
		});
	std::printf("Pending work total: %d\n", total);

	std::vector<Device> queued(std::begin(devices), std::end(devices));
	auto removed =
		hx::algo::erase_if(queued, [](const Device &device) noexcept {
			return device.pending == 0;
		});
	std::printf("Removed completed devices: %zu\n",
		static_cast<size_t>(removed));

	// Linked-list iterators are not random access. Sort the whole list.
	std::list<int> priorities = {3, 1, 2};
	hx::algo::sort(priorities);
	std::printf("First linked-list priority: %d\n", priorities.front());

	return 0;
}
