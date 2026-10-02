#include <hx/Variant.hpp>
#include "cpp_variant_fixture.hpp"
#include "cpp_check.hpp"

struct TrackedValue {
	TrackedValue *self;
	int *live;
	int id;
	TrackedValue(int &count, int number) noexcept
	    : self(this)
	    , live(&count)
	    , id(number)
	{
		++*live;
	}
	TrackedValue(const TrackedValue &other) noexcept
	    : self(this)
	    , live(other.live)
	    , id(other.id)
	{
		++*live;
	}
	~TrackedValue() noexcept
	{
		--*live;
	}
};
struct MoveOnlyValue {
	int *live;
	int id;
	MoveOnlyValue(int &count, int number) noexcept
	    : live(&count)
	    , id(number)
	{
		++*live;
	}
	MoveOnlyValue(const MoveOnlyValue &) = delete;
	MoveOnlyValue &operator=(const MoveOnlyValue &) = delete;
	~MoveOnlyValue() noexcept
	{
		--*live;
	}
};
struct alignas(64) AlignedValue {
	int id;
	explicit AlignedValue(int value) noexcept
	    : id(value)
	{
	}
};
struct ValueVisitor {
	int *visited;
	void operator()(int32_t value) const noexcept
	{
		*visited = value;
	}
	void operator()(double value) const noexcept
	{
		*visited = static_cast<int>(value);
	}
	void operator()(hx::StringView value) const noexcept
	{
		*visited = static_cast<int>(value.size());
	}
	void operator()(const VariantPoint &value) const noexcept
	{
		*visited = static_cast<int>(value.x + value.y);
	}
	void operator()(hx::EmptyValue) const noexcept
	{
		*visited = -1;
	}
};
struct FailingAllocator {
	size_t attempts;
	size_t fail;
	size_t live;
};
static void *HX_OS_CALL fail_allocate(size_t bytes, void *user)
{
	auto *state = static_cast<FailingAllocator *>(user);
	if (++state->attempts == state->fail)
		return NULL;
	void *memory = std::malloc(bytes);
	if (memory)
		++state->live;
	return memory;
}
static void HX_OS_CALL fail_free(void *memory, void *user)
{
	--static_cast<FailingAllocator *>(user)->live;
	std::free(memory);
}
static void TestCommonValues() noexcept
{
	hx::Variant value;
	CHECK(value.empty() && value.holds<hx::EmptyValue>());
	CHECK(value.get<int32_t>().status() == HX_OS_TYPE_MISMATCH);
	CHECK(value.set(int32_t(42)));
	CHECK(value.holds<int32_t>() && value.get<int32_t>().value() == 42);
	CHECK(value.get<int64_t>().status() == HX_OS_TYPE_MISMATCH);
	CHECK(value.emplace<int32_t>(7));
	CHECK(value.kind() == HX_VARIANT_I32 &&
		value.get<int32_t>().value() == 7);
	CHECK(value.emplace<double>(3.5));
	CHECK(value.get<double>().value() == 3.5);
	CHECK(value.set(true) && value.get<bool>().value());
	CHECK(value.set(uint64_t(UINT64_MAX)));
	CHECK(value.get<uint64_t>().value() == UINT64_MAX);
	CHECK(value.set("hello"));
	CHECK(value.holds<hx::StringView>());
	CHECK(value.get<hx::StringView>().value().size() == 5);
	auto clone = value.clone();
	CHECK(clone);
	CHECK(value.emplace<hx::StringView>("a\0b", 3));
	CHECK(value.get<hx::StringView>().value().size() == 3);
	CHECK(clone.value().get<hx::StringView>().value().size() == 5);
	auto owned = value.string();
	CHECK(owned && owned.value().size() == 3);
	CHECK(value.takeString(std::move(owned).value()));
	CHECK(owned.value().empty());
	int visited = 0;
	CHECK((value.visit<int32_t, hx::StringView>(ValueVisitor{&visited})));
	CHECK(visited == 3);
	CHECK(value.visit<double>(ValueVisitor{&visited}).status() ==
		HX_OS_TYPE_MISMATCH);
	CHECK(value.set(int32_t(19)));
	CHECK(value.visit<int32_t>([](int32_t) noexcept {
			   return HX_OS_TIMEOUT;
		   }).status() == HX_OS_TIMEOUT);
	CHECK(value.visit<int32_t>([](int32_t) noexcept -> hx::Result<void> {
			   hx_os_error error = {
				   HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 71, 19};
			   return hx::Failure(error);
		   })
			.error()
			.native_code == 71);
	void (*missing)(int32_t) = NULL;
	CHECK(value.visit<int32_t>(missing).status() == HX_OS_INVALID);
	CHECK(value.set(hx::EmptyValue{}) && value.empty());
	CHECK(value.get<hx::EmptyValue>());
}
static void TestObjects() noexcept
{
	int live = 0;
	{
		hx::Variant value;
		CHECK(SetPointFromOtherFile(value));
		CHECK(value.holds<VariantPoint>());
		CHECK(value.get<VariantPoint>().value().x == 2.5);
		CHECK(value.emplace<TrackedValue>(live, 17));
		auto object = value.get<TrackedValue>();
		CHECK(live == 1 && object &&
			object.value().self == &object.value());
		auto clone = value.clone();
		CHECK(clone && live == 2);
		CHECK(&clone.value().get<TrackedValue>().value() !=
			&object.value());
		CHECK(clone.value().get<TrackedValue>().value().self ==
			&clone.value().get<TrackedValue>().value());
		const hx::Variant &constant = value;
		static_assert(
			std::is_same<decltype(constant.get<TrackedValue>()),
				hx::Result<const TrackedValue &>>::value,
			"Const access");
		hx::Variant moved(std::move(value));
		CHECK(value.empty() &&
			moved.get<TrackedValue>().value().id == 17);
		CHECK(&moved.get<TrackedValue>().value() == &object.value());
		moved.swap(value);
		CHECK(moved.empty() && live == 2);
		value.clear();
		CHECK(live == 1);
		CHECK(moved.emplace<MoveOnlyValue>(live, 9));
		CHECK(live == 2);
		CHECK(moved.clone().status() == HX_OS_NOT_SUPPORTED);
		CHECK(moved.get<MoveOnlyValue>().value().id == 9);
		CHECK(value.emplace<AlignedValue>(21));
		CHECK(!(reinterpret_cast<uintptr_t>(
				&value.get<AlignedValue>().value()) &
			63));
	}
	CHECK(live == 0);
}
static void TestTypedValues() noexcept
{
	typedef hx::TypedVariant<int32_t, double, hx::StringView, VariantPoint,
		hx::EmptyValue>
		Value;
	Value value;
	int visited = 0;
	CHECK(value.visit(ValueVisitor{&visited}) && visited == -1);
	CHECK(value.set(VariantPoint{2, 5}));
	CHECK(value.visit(ValueVisitor{&visited}) && visited == 7);
	CHECK(value.set("hello"));
	CHECK(value.visit(ValueVisitor{&visited}) && visited == 5);
	CHECK(value.emplace<int32_t>(23));
	CHECK(value.visit(ValueVisitor{&visited}) && visited == 23);
	auto clone = value.clone();
	CHECK(clone && clone.value().get<int32_t>().value() == 23);
	CHECK(value.set(3.5));
	CHECK(clone.value().get<int32_t>().value() == 23);
	hx::TypedVariant<int32_t> noEmpty;
	CHECK(noEmpty.visit(ValueVisitor{&visited}).status() ==
		HX_OS_TYPE_MISMATCH);
}
static void TestFailureAndScalarCost() noexcept
{
	FailingAllocator allocator = {};
	hx_os_context_options options;
	hx_os_context_options_init(&options);
	options.allocator = {fail_allocate, fail_free, &allocator};
	hx_os_context *context = NULL;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	{
		hx::Variant value(context);
		size_t attempts = allocator.attempts;
		for (int i = 0; i < 1000; ++i) {
			CHECK(value.set(int32_t(i)));
			CHECK(value.emplace<double>(i + 0.5));
		}
		CHECK(allocator.attempts == attempts);
		CHECK(value.set(int32_t(7)));
		allocator.fail = allocator.attempts + 1;
		CHECK(value.set(VariantPoint{1, 2}).status() ==
			HX_OS_NO_MEMORY);
		CHECK(value.get<int32_t>().value() == 7);
		allocator.fail = allocator.attempts + 1;
		CHECK(value.set("new string").status() == HX_OS_NO_MEMORY);
		CHECK(value.get<int32_t>().value() == 7);
		allocator.fail = 0;
		CHECK(value.set(VariantPoint{1, 2}));
		CHECK(hx_os_context_allocations(context) == 1);
	}
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!allocator.live);
}
int main()
{
	static_assert(sizeof(hx::Variant) == sizeof(hx_variant),
		"Facade must not add storage");
	if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT)) {
		hx::Variant value;
		CHECK(value.set(int32_t(7)).status() == HX_OS_NOT_SUPPORTED);
		CHECK(value.emplace<double>(1).status() == HX_OS_NOT_SUPPORTED);
		CHECK(value.set(VariantPoint{1, 2}).status() ==
			HX_OS_NOT_SUPPORTED);
		CHECK(value.get<int32_t>().status() == HX_OS_NOT_SUPPORTED);
		CHECK(value.clone().status() == HX_OS_NOT_SUPPORTED);
		CHECK(value.set(hx::EmptyValue{}).status() ==
			HX_OS_NOT_SUPPORTED);
	}
	else {
		TestCommonValues();
		TestObjects();
		TestTypedValues();
		TestFailureAndScalarCost();
	}
	std::printf("C++ variant contracts passed; value=%zu align=%zu\n",
		sizeof(hx::Variant), alignof(hx::Variant));
}
