#ifndef HX_C_PLATFORM_CPP_VARIANT_HPP
#define HX_C_PLATFORM_CPP_VARIANT_HPP

#include <hx_variant.h>
#include <hx/Values.hpp>
#include <hx/Callback.hpp>
#include <tuple>

namespace hx {
struct EmptyValue {};
namespace detail {
template <typename T>
struct VariantAllowed
    : std::integral_constant<bool,
	      std::is_object<T>::value && !std::is_array<T>::value &&
		      std::is_same<T,
			      typename std::remove_cv<T>::type>::value> {};
template <typename... Types> struct VariantAllowedList;
template <> struct VariantAllowedList<> : std::true_type {};
template <typename First, typename... Rest>
struct VariantAllowedList<First, Rest...>
    : std::integral_constant<bool,
	      VariantAllowed<First>::value &&
		      VariantAllowedList<Rest...>::value> {};
template <typename T, bool Copyable> struct VariantCopyOperation {
	static constexpr hx_variant_copy_fn function() noexcept
	{
		return NULL;
	}
};

template <typename T> struct VariantCopyOperation<T, true> {
	static hx_os_status HX_OS_CALL copy_object(void *destination,
		const void *source, hx_os_context *context, void *user,
		hx_os_error *error) noexcept
	{
		(void)context;
		(void)user;
		new (destination) T(*static_cast<const T *>(source));
		hx_os_error_clear(error);

		return HX_OS_OK;
	}

	static constexpr hx_variant_copy_fn function() noexcept
	{
		return copy_object;
	}
};

template <typename T, bool Trivial> struct VariantDropOperation {
	static void HX_OS_CALL drop_object(void *object, void *user) noexcept
	{
		(void)user;
		static_cast<T *>(object)->~T();
	}

	static constexpr hx_variant_drop_fn function() noexcept
	{
		return drop_object;
	}
};

template <typename T> struct VariantDropOperation<T, true> {
	static constexpr hx_variant_drop_fn function() noexcept
	{
		return NULL;
	}
};

template <typename T> struct VariantObjectType {
	static const hx_variant_type *type() noexcept
	{
		static_assert(VariantAllowed<T>::value,
			"Custom values must be unqualified, non-array object "
			"types");
		static_assert(std::is_nothrow_destructible<T>::value,
			"Variant objects need noexcept destruction");

		typedef VariantCopyOperation<T,
			std::is_nothrow_copy_constructible<T>::value>
			Copy;
		typedef VariantDropOperation<T,
			std::is_trivially_destructible<T>::value>
			Drop;
		static const hx_variant_type value = {sizeof(T), alignof(T),
			Copy::function(), Drop::function(), NULL,
			std::is_nothrow_copy_constructible<T>::value
				? 0u
				: static_cast<uint32_t>(
					  HX_VARIANT_TYPE_NO_COPY)};

		return &value;
	}
};

template <typename T> struct VariantAccess {
	typedef std::false_type Native;

	static bool holds(const hx_variant *value) noexcept
	{
		return hx_variant_custom_type(value) ==
			VariantObjectType<T>::type();
	}

	static hx_os_status set(
		hx_variant *value, const T &input, hx_os_error *error) noexcept
	{
		static_assert(std::is_nothrow_copy_constructible<T>::value,
			"Use emplace for move-only values, or a C descriptor "
			"for fallible copying");

		return hx_variant_set_custom(
			value, VariantObjectType<T>::type(), &input, error);
	}

	static Result<T &> get(hx_variant *value) noexcept
	{
		void *object = NULL;
		hx_os_error error = {};

		auto status = hx_variant_get_custom_mut(
			value, VariantObjectType<T>::type(), &object, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return *static_cast<T *>(object);
	}

	static Result<const T &> get(const hx_variant *value) noexcept
	{
		const void *object = NULL;
		hx_os_error error = {};

		auto status = hx_variant_get_custom(
			value, VariantObjectType<T>::type(), &object, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return *static_cast<const T *>(object);
	}
};
#define HX_CPP_VARIANT_SCALAR(type, name, tag)                                 \
	template <> struct VariantAccess<type> {                               \
		typedef std::true_type Native;                                 \
		static bool holds(const hx_variant *value) noexcept            \
		{                                                              \
			return hx_variant_kind(value) == tag;                  \
		}                                                              \
		static hx_os_status set(hx_variant *value, type input,         \
			hx_os_error *error) noexcept                           \
		{                                                              \
			return hx_variant_set_##name(value, input, error);     \
		}                                                              \
		static Result<type> get(const hx_variant *value) noexcept      \
		{                                                              \
			type output{};                                         \
			hx_os_error error = {};                                \
			auto status =                                          \
				hx_variant_get_##name(value, &output, &error); \
			if (status != HX_OS_OK)                                \
				return Failure(status, error);                 \
			return output;                                         \
		}                                                              \
	};
HX_CPP_VARIANT_SCALAR(int32_t, i32, HX_VARIANT_I32)

HX_CPP_VARIANT_SCALAR(uint32_t, u32, HX_VARIANT_U32)

HX_CPP_VARIANT_SCALAR(int64_t, i64, HX_VARIANT_I64)

HX_CPP_VARIANT_SCALAR(uint64_t, u64, HX_VARIANT_U64)

HX_CPP_VARIANT_SCALAR(float, float, HX_VARIANT_FLOAT)

HX_CPP_VARIANT_SCALAR(double, double, HX_VARIANT_DOUBLE)

HX_CPP_VARIANT_SCALAR(void *, pointer, HX_VARIANT_POINTER)
#undef HX_CPP_VARIANT_SCALAR
template <> struct VariantAccess<bool> {
	typedef std::true_type Native;

	static bool holds(const hx_variant *value) noexcept
	{
		return hx_variant_kind(value) == HX_VARIANT_BOOL;
	}

	static hx_os_status set(
		hx_variant *value, bool input, hx_os_error *error) noexcept
	{
		return hx_variant_set_bool(value, input, error);
	}

	static Result<bool> get(const hx_variant *value) noexcept
	{
		int output = 0;
		hx_os_error error = {};

		auto status = hx_variant_get_bool(value, &output, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return output != 0;
	}
};

template <> struct VariantAccess<StringView> {
	typedef std::true_type Native;

	static bool holds(const hx_variant *value) noexcept
	{
		return hx_variant_kind(value) == HX_VARIANT_STRING;
	}

	static hx_os_status set(hx_variant *value, StringView input,
		hx_os_error *error) noexcept
	{
		return hx_variant_set_string(value, input.native(), error);
	}

	static Result<StringView> get(const hx_variant *value) noexcept
	{
		hx_string_view output = {};
		hx_os_error error = {};
		auto status =
			hx_variant_get_string_view(value, &output, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringView(output);
	}
};

template <> struct VariantAccess<EmptyValue> {
	typedef std::true_type Native;

	static bool holds(const hx_variant *value) noexcept
	{
		return hx_variant_kind(value) == HX_VARIANT_EMPTY;
	}

	static hx_os_status set(
		hx_variant *value, EmptyValue, hx_os_error *error) noexcept
	{
		if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT)) {
			if (error)
				*error = {HX_OS_NOT_SUPPORTED, 0, 0, 0};

			return HX_OS_NOT_SUPPORTED;
		}

		hx_variant_clear(value);
		hx_os_error_clear(error);

		return HX_OS_OK;
	}

	static Result<EmptyValue> get(const hx_variant *value) noexcept
	{
		if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT))
			return Failure(HX_OS_NOT_SUPPORTED);

		return holds(value)
			? Result<EmptyValue>(EmptyValue{})
			: Result<EmptyValue>(Failure(HX_OS_TYPE_MISMATCH));
	}
};

template <size_t... Index> struct VariantIndices {};
template <size_t Count, size_t... Index>
struct MakeVariantIndices : MakeVariantIndices<Count - 1, Count - 1, Index...> {
};

template <size_t... Index> struct MakeVariantIndices<0, Index...> {
	typedef VariantIndices<Index...> type;
};

template <typename T, typename... Args> struct VariantConstructor {
	std::tuple<Args &&...> arguments;

	template <size_t... Index>
	void construct(void *destination, VariantIndices<Index...>) noexcept
	{
		new (destination)
			T(std::forward<Args>(std::get<Index>(arguments))...);
	}

	static hx_os_status HX_OS_CALL construct_object(void *destination,
		hx_os_context *context, void *user, hx_os_error *error) noexcept
	{
		(void)context;

		auto *self = static_cast<VariantConstructor *>(user);

		self->construct(destination,
			typename MakeVariantIndices<sizeof...(Args)>::type());
		hx_os_error_clear(error);

		return HX_OS_OK;
	}
};

template <typename... Types> struct VariantVisitor;
template <> struct VariantVisitor<> {
	template <typename F>
	static Result<void> call(const hx_variant *, F &) noexcept
	{
		return Failure(HX_OS_TYPE_MISMATCH);
	}
};

template <typename T, typename... Rest> struct VariantVisitor<T, Rest...> {
	template <typename F>
	static Result<void> call(const hx_variant *value, F &function) noexcept
	{
		if (!VariantAccess<T>::holds(value))
			return VariantVisitor<Rest...>::call(value, function);

		auto result = VariantAccess<T>::get(value);
		if (!result)
			return Failure(result.error());

		return InvokeCallback(function, result.value());
	}
};

template <typename T, typename... Types> struct VariantContains;
template <typename T> struct VariantContains<T> : std::false_type {};
template <typename T, typename First, typename... Rest>
struct VariantContains<T, First, Rest...>
    : std::integral_constant<bool,
	      std::is_same<T, First>::value ||
		      VariantContains<T, Rest...>::value> {};
template <typename... Types> struct VariantUnique;
template <> struct VariantUnique<> : std::true_type {};
template <typename First, typename... Rest>
struct VariantUnique<First, Rest...>
    : std::integral_constant<bool,
	      !VariantContains<First, Rest...>::value &&
		      VariantUnique<Rest...>::value> {};
} // namespace detail

/* Move-only owner; clone is explicit/fallible. Custom getters borrow stable
 * separately constructed objects. Scalar getters copy; strings return views.
 */
class Variant {
public:
	explicit Variant(hx_os_context *context = NULL) noexcept
	    : m_value_{}
	{
		hx_variant_init(&m_value_, context);
	}

	Variant(Variant &&other) noexcept
	    : m_value_{}
	{
		hx_variant_move(&m_value_, &other.m_value_);
	}

	Variant &operator=(Variant &&other) noexcept
	{
		hx_variant_move(&m_value_, &other.m_value_);

		return *this;
	}

	Variant(const Variant &) = delete;

	Variant &operator=(const Variant &) = delete;

	~Variant() noexcept
	{
		hx_variant_destroy(&m_value_);
	}

	template <typename T>
	static Result<Variant> from(
		const T &input, hx_os_context *context = NULL) noexcept
	{
		Variant value(context);

		auto result = value.set(input);
		if (!result)
			return Failure(result.error());

		return value;
	}

	template <typename T>
	typename std::enable_if<
		!std::is_convertible<const T &, StringView>::value,
		Result<void>>::type
	set(const T &input) noexcept
	{
		hx_os_error error = {};
		auto status =
			detail::VariantAccess<T>::set(&m_value_, input, &error);

		return detail::Status(status, error);
	}

	Result<void> set(StringView input) noexcept
	{
		hx_os_error error = {};

		auto status = hx_variant_set_string(
			&m_value_, input.native(), &error);

		return detail::Status(status, error);
	}

	Result<void> takeString(String &&input) noexcept
	{
		hx_os_error error = {};

		auto status = hx_variant_take_string(
			&m_value_, input.native(), &error);

		return detail::Status(status, error);
	}

	template <typename T, typename... Args>
	Result<void> emplace(Args &&...arguments) noexcept
	{
		static_assert(
			std::is_nothrow_constructible<T, Args &&...>::value,
			"Construct without failure, or use C emplace "
			"callbacks");

		return emplaceImpl<T>(
			typename detail::VariantAccess<T>::Native(),
			std::forward<Args>(arguments)...);
	}

	template <typename T> bool holds() const noexcept
	{
		return detail::VariantAccess<T>::holds(&m_value_);
	}

	template <typename T>
	auto get() & noexcept -> decltype(detail::VariantAccess<T>::get(
		static_cast<hx_variant *>(NULL)))
	{
		return detail::VariantAccess<T>::get(&m_value_);
	}

	template <typename T>
	auto get() const & noexcept -> decltype(detail::VariantAccess<T>::get(
		static_cast<const hx_variant *>(NULL)))
	{
		return detail::VariantAccess<T>::get(&m_value_);
	}

	template <typename T>
	auto get() const && -> decltype(detail::VariantAccess<T>::get(
		static_cast<const hx_variant *>(NULL))) = delete;

	Result<String> string(hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[this](hx_string *out, hx_os_error *error) noexcept {
				return hx_variant_get_string(
					&m_value_, out, error);
			},
			context);
	}

	Result<Variant> clone(hx_os_context *context = NULL) const noexcept
	{
		Variant value(context);

		hx_os_error error = {};
		auto status =
			hx_variant_copy(&value.m_value_, &m_value_, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	template <typename... Types, typename F>
	Result<void> visit(F &&function) const & noexcept
	{
		static_assert(sizeof...(Types) > 0 && sizeof...(Types) <= 64,
			"Declare 1..64 visitor alternatives");
		if (!(hx_os_capabilities() & HX_OS_CAP_VARIANT))
			return Failure(HX_OS_NOT_SUPPORTED);

		typedef typename std::remove_reference<F>::type Function;

		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		return detail::VariantVisitor<Types...>::call(
			&m_value_, function);
	}

	void clear() noexcept
	{
		hx_variant_clear(&m_value_);
	}

	void swap(Variant &other) noexcept
	{
		hx_variant_swap(&m_value_, &other.m_value_);
	}

	int kind() const noexcept
	{
		return hx_variant_kind(&m_value_);
	}

	bool empty() const noexcept
	{
		return kind() == HX_VARIANT_EMPTY;
	}

	hx_os_context *context() const noexcept
	{
		return m_value_.context;
	}

	hx_variant *native() noexcept
	{
		return &m_value_;
	}

	const hx_variant *native() const noexcept
	{
		return &m_value_;
	}

private:
	template <typename T, typename... Args>
	Result<void> emplaceImpl(std::true_type, Args &&...arguments) noexcept
	{
		return set(T(std::forward<Args>(arguments)...));
	}

	template <typename T, typename... Args>
	Result<void> emplaceImpl(std::false_type, Args &&...arguments) noexcept
	{
		detail::VariantConstructor<T, Args...> constructor = {
			std::forward_as_tuple(
				std::forward<Args>(arguments)...)};
		hx_os_error error = {};

		auto status = hx_variant_emplace_custom(&m_value_,
			detail::VariantObjectType<T>::type(),
			detail::VariantConstructor<T,
				Args...>::construct_object,
			&constructor, &error);

		return detail::Status(status, error);
	}

	hx_variant m_value_;
};

/* A closed set of alternatives over the same C implementation. StringView
 * is the canonical string alternative: bytes are owned, getters borrow them.
 */
template <typename... Types> class TypedVariant {
	static_assert(sizeof...(Types) > 0 && sizeof...(Types) <= 64,
		"Declare 1..64 alternatives");
	static_assert(detail::VariantUnique<Types...>::value,
		"Alternatives must be unique");
	static_assert(detail::VariantAllowedList<Types...>::value,
		"Alternatives must be unqualified, non-array object types");
	static_assert(!detail::VariantContains<String, Types...>::value,
		"Use StringView as the owned string alternative");

public:
	explicit TypedVariant(hx_os_context *context = NULL) noexcept
	    : m_value_(context)
	{
	}

	TypedVariant(TypedVariant &&) noexcept = default;

	TypedVariant &operator=(TypedVariant &&) noexcept = default;

	TypedVariant(const TypedVariant &) = delete;

	TypedVariant &operator=(const TypedVariant &) = delete;

	template <typename T>
	typename std::enable_if<
		!std::is_convertible<const T &, StringView>::value,
		Result<void>>::type
	set(const T &input) noexcept
	{
		static_assert(detail::VariantContains<T, Types...>::value,
			"Type is not a declared alternative");

		return m_value_.set(input);
	}

	Result<void> set(StringView input) noexcept
	{
		static_assert(
			detail::VariantContains<StringView, Types...>::value,
			"Declare StringView to store UTF-8 strings");

		return m_value_.set(input);
	}

	template <typename T, typename... Args>
	Result<void> emplace(Args &&...arguments) noexcept
	{
		static_assert(detail::VariantContains<T, Types...>::value,
			"Type is not a declared alternative");

		return m_value_.template emplace<T>(
			std::forward<Args>(arguments)...);
	}

	template <typename T> bool holds() const noexcept
	{
		static_assert(detail::VariantContains<T, Types...>::value,
			"Type is not a declared alternative");

		return m_value_.template holds<T>();
	}

	template <typename T>
	auto get() & noexcept -> decltype(std::declval<Variant &>().get<T>())
	{
		static_assert(detail::VariantContains<T, Types...>::value,
			"Type is not a declared alternative");

		return m_value_.template get<T>();
	}

	template <typename T>
	auto get() const & noexcept
		-> decltype(std::declval<const Variant &>().get<T>())
	{
		static_assert(detail::VariantContains<T, Types...>::value,
			"Type is not a declared alternative");

		return m_value_.template get<T>();
	}

	template <typename T>
	auto get() const && -> decltype(std::declval<const Variant &>()
						.get<T>()) = delete;
	template <typename F> Result<void> visit(F &&function) const & noexcept
	{
		return m_value_.template visit<Types...>(
			std::forward<F>(function));
	}

	Result<TypedVariant> clone(hx_os_context *context = NULL) const noexcept
	{
		TypedVariant value(context);

		hx_os_error error = {};

		auto status = hx_variant_copy(
			value.m_value_.native(), m_value_.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	void clear() noexcept
	{
		m_value_.clear();
	}

	bool empty() const noexcept
	{
		return m_value_.empty();
	}

private:
	Variant m_value_;
};
} // namespace hx
#endif
