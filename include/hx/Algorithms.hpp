#ifndef HX_C_PLATFORM_CPP_ALGORITHMS_HPP
#define HX_C_PLATFORM_CPP_ALGORITHMS_HPP

#include <algorithm>
#include <cstddef>
#include <forward_list>
#include <iterator>
#include <list>
#include <numeric>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

namespace hx {
namespace algo {
/* Standard C++11 semantics and iterator requirements apply. These aliases
 * add no error layer or storage. Element operations, predicates and output
 * iterators must not raise exceptions. Output capacity belongs to the caller.
 * sort/partial_sort/nth_element require random-access iterators. Bounds and
 * binary search require appropriately ordered input. unique/remove_if return
 * a logical new end; they do not erase container elements.
 */
using std::accumulate;
using std::adjacent_difference;
using std::all_of;
using std::any_of;
using std::binary_search;
using std::copy_if;
using std::count;
using std::count_if;
using std::equal;
using std::equal_range;
using std::find;
using std::find_if;
using std::inner_product;
using std::iota;
using std::is_sorted;
using std::lower_bound;
using std::max_element;
using std::merge;
using std::min_element;
using std::minmax_element;
using std::mismatch;
using std::none_of;
using std::nth_element;
using std::partial_sort;
using std::partial_sum;
using std::partition;
using std::remove_if;
using std::replace_if;
using std::reverse;
using std::rotate;
using std::set_difference;
using std::set_intersection;
using std::set_symmetric_difference;
using std::set_union;
using std::shuffle;
using std::sort;
using std::transform;
using std::unique;
using std::upper_bound;

namespace detail {
template <typename Input, typename Output, typename Generator>
Output sample_range(Input first, Input last, Output out, std::size_t count,
	Generator &generator, std::forward_iterator_tag)
{
	if (!count)
		return out;
	std::size_t remaining =
		static_cast<std::size_t>(std::distance(first, last));

	if (count > remaining)
		count = remaining;
	for (; count; ++first, --remaining) {
		std::uniform_int_distribution<std::size_t> pick(
			0, remaining - 1);

		if (pick(generator) < count) {
			*out++ = *first;
			--count;
		}
	}
	return out;
}

template <typename Input, typename Output, typename Generator>
Output sample_range(Input first, Input last, Output out, std::size_t count,
	Generator &generator, std::input_iterator_tag)
{
	static_assert(std::is_base_of<std::random_access_iterator_tag,
			      typename std::iterator_traits<
				      Output>::iterator_category>::value,
		"Single-pass sampling requires random-access output");
	std::size_t seen = 0;

	if (!count)
		return out;
	for (; first != last && seen < count; ++first, ++seen)
		out[seen] = *first;
	for (; first != last; ++first, ++seen) {
		std::uniform_int_distribution<std::size_t> pick(0, seen);
		std::size_t index = pick(generator);

		if (index < count)
			out[index] = *first;
	}
	return out + (seen < count ? seen : count);
}
} // namespace detail

/* C++11 sampling without replacement. Forward input supports ordinary
 * output iterators and preserves order; single-pass input needs random-access
 * output for reservoir replacement. Caller provides count output slots and
 * a valid standard uniform random generator. No library allocation occurs.
 */
template <typename Input, typename Output, typename Generator>
Output sample(Input first, Input last, Output out, std::size_t count,
	Generator &generator)
{
	return detail::sample_range(first, last, out, count, generator,
		typename std::iterator_traits<Input>::iterator_category());
}

/* Whole linked containers use their stable node-relinking sorts. Their
 * iterators still cannot be passed to the random-access range sort above.
 * Sorting does not allocate nodes or copy/move the stored elements.
 */
template <typename T, typename Allocator>
void sort(std::list<T, Allocator> &values)
{
	values.sort();
}

template <typename T, typename Allocator, typename Compare>
void sort(std::list<T, Allocator> &values, Compare compare)
{
	values.sort(std::move(compare));
}

template <typename T, typename Allocator>
void sort(std::forward_list<T, Allocator> &values)
{
	values.sort();
}

template <typename T, typename Allocator, typename Compare>
void sort(std::forward_list<T, Allocator> &values, Compare compare)
{
	values.sort(std::move(compare));
}

/* Explicit container overloads erase values and return the removed count.
 * Iterator invalidation follows the container; predicates, element movement
 * and destruction must not raise exceptions. No result array is allocated.
 */
template <typename T, typename Allocator, typename Predicate>
typename std::vector<T, Allocator>::size_type erase_if(
	std::vector<T, Allocator> &values, Predicate predicate)
{
	auto size = values.size();

	values.erase(std::remove_if(values.begin(), values.end(),
			     std::move(predicate)),
		values.end());

	return size - values.size();
}

template <typename T, typename Allocator, typename Predicate>
typename std::list<T, Allocator>::size_type erase_if(
	std::list<T, Allocator> &values, Predicate predicate)
{
	auto size = values.size();

	values.remove_if(std::move(predicate));

	return size - values.size();
}

template <typename T, typename Allocator, typename Predicate>
typename std::forward_list<T, Allocator>::size_type erase_if(
	std::forward_list<T, Allocator> &values, Predicate predicate)
{
	typename std::forward_list<T, Allocator>::size_type removed = 0;
	auto before = values.before_begin();
	auto current = values.begin();

	while (current != values.end()) {
		if (predicate(*current)) {
			current = values.erase_after(before);
			++removed;
		}
		else {
			before = current;
			++current;
		}
	}

	return removed;
}

} // namespace algo
} // namespace hx
#endif
