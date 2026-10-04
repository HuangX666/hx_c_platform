#ifndef HX_C_PLATFORM_CPP_FILES_HPP
#define HX_C_PLATFORM_CPP_FILES_HPP

#include <hx_os_file.h>
#include <hx/Values.hpp>
#include <hx/Callback.hpp>

namespace hx {

class File {
public:
	File() noexcept
	    : m_file_(NULL)
	{
	}

	static File adopt(hx_os_file *&owner) noexcept
	{
		File result;

		result.m_file_ = owner;
		owner = NULL;

		return result;
	}

	static Result<File> open(const char *path,
		uint32_t flags = HX_OS_FILE_READ,
		hx_os_context *context = NULL) noexcept
	{
		File result;
		hx_os_error error = {};

		auto status = hx_os_file_open(
			context, path, flags, &result.m_file_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	File(const File &) = delete;

	File &operator=(const File &) = delete;

	File(File &&other) noexcept
	    : m_file_(other.m_file_)
	{
		other.m_file_ = NULL;
	}

	File &operator=(File &&other) noexcept
	{
		if (this != &other) {
			(void)hx_os_file_close(&m_file_, NULL);
			m_file_ = other.m_file_;
			other.m_file_ = NULL;
		}

		return *this;
	}

	~File() noexcept
	{
		(void)hx_os_file_close(&m_file_, NULL);
	}

	struct ReadResult {
		size_t bytes;
		bool eof;
	};

	/* A failed result can still own a progress value. EOF is kept as
	 * status. */
	Result<ReadResult> readSome(void *data, size_t bytes) noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_file_read_some(
			m_file_, data, bytes, &completed, &error);

		return Result<ReadResult>::withValue(
			ReadResult{completed, status == HX_OS_EOF}, status,
			error);
	}

	/* EOF is successful completion with eof=true. Other failures retain
	 * partial bytes just as readSome does. */
	Result<ReadResult> read(void *data, size_t bytes) noexcept
	{
		auto result = readSome(data, bytes);
		if (result.status() == HX_OS_EOF && result.hasValue()) {
			hx_os_error error = {};

			return Result<ReadResult>::withValue(
				result.value(), HX_OS_OK, error);
		}

		return result;
	}

	Result<size_t> writeAll(const void *data, size_t bytes) noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_file_write_all(
			m_file_, data, bytes, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<ReadResult> readAt(
		uint64_t offset, void *data, size_t bytes) noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_file_read_at(
			m_file_, offset, data, bytes, &completed, &error);

		return Result<ReadResult>::withValue(
			ReadResult{completed, status == HX_OS_EOF}, status,
			error);
	}

	Result<size_t> writeAt(
		uint64_t offset, const void *data, size_t bytes) noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_file_write_at(
			m_file_, offset, data, bytes, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<void> sync() noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};

		auto status = hx_os_file_sync(m_file_, &error);

		return detail::Status(status, error);
	}

	Result<uint64_t> size() const noexcept
	{
		if (!m_file_)
			return Failure(HX_OS_CLOSED);

		uint64_t value = 0;
		hx_os_error error = {};

		auto status = hx_os_file_size(m_file_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_file_close(&m_file_, &error);

		return detail::Status(status, error);
	}

	hx_os_file *get() const noexcept
	{
		return m_file_;
	}

private:
	hx_os_file *m_file_;
};

class Directory {
public:
	static Result<String> findFirstFile(const char *root,
		const char *pattern, const hx_os_walk_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_directory_find_first_file_ex(
					root, pattern, options, out, error);
			},
			context);
	}

	/* Recursive basename search. Paths are borrowed during the callback.
	 * false/EOF stops normally, matching forEach's callback contract. */
	template <typename F>
	static Result<size_t> findFiles(const char *root, const char *pattern,
		F &&function, const hx_os_walk_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		typedef typename std::remove_reference<F>::type Function;

		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		Visitor<Function> visitor = {&function};
		size_t completed = 0;
		hx_os_error error = {};

		auto status = hx_os_directory_find_files_ex(context, root,
			pattern, options, visit<Function>, &visitor, &completed,
			&error);

		return Result<size_t>::withValue(completed, status, error);
	}

	/* Callback borrows each name only during its call. A bool callback
	 * continues on true and stops normally on false;
	 * void/status/Result<void> callbacks follow the usual explicit-error
	 * contract. */
	template <typename F>
	static Result<size_t> forEach(const char *path, F &&function,
		size_t maxEntries = 65536,
		hx_os_context *context = NULL) noexcept
	{
		typedef typename std::remove_reference<F>::type Function;

		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		Visitor<Function> visitor = {&function};
		size_t completed = 0;
		hx_os_error error = {};
		auto status =
			hx_os_directory_visit_ex(context, path, maxEntries,
				visit<Function>, &visitor, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	/* Entry bytes include NUL. Moving the entry keeps its name alive. */
	struct Entry {
		Buffer storage;
		unsigned kind;
		bool kindKnown;

		const char *name() const noexcept
		{
			return reinterpret_cast<const char *>(storage.data());
		}
	};

	Directory() noexcept
	    : m_directory_(NULL)
	    , m_context_(NULL)
	{
	}

	static Result<Directory> open(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		Directory result;
		hx_os_error error = {};

		auto status = hx_os_directory_open(
			context, path, &result.m_directory_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		result.m_context_ = context;

		return result;
	}

	Directory(const Directory &) = delete;

	Directory &operator=(const Directory &) = delete;

	Directory(Directory &&other) noexcept
	    : m_directory_(other.m_directory_)
	    , m_context_(other.m_context_)
	{
		other.m_directory_ = NULL;
		other.m_context_ = NULL;
	}

	Directory &operator=(Directory &&other) noexcept
	{
		if (this != &other) {
			hx_os_directory_close(m_directory_);
			m_directory_ = other.m_directory_;
			m_context_ = other.m_context_;
			other.m_directory_ = NULL;
			other.m_context_ = NULL;
		}

		return *this;
	}

	~Directory() noexcept
	{
		hx_os_directory_close(m_directory_);
	}

	Result<Entry> next(bool queryKind = false) noexcept
	{
		if (!m_directory_)
			return Failure(HX_OS_CLOSED);

		size_t capacity = 256;

		for (;;) {
			auto buffer = Buffer::allocate(capacity, m_context_);
			if (!buffer)
				return Failure(buffer.error());

			size_t required = 0;
			unsigned kind = HX_OS_FILE_OTHER;
			hx_os_error error = {};
			auto *name =
				reinterpret_cast<char *>(buffer.value().data());
			auto status = queryKind
				? hx_os_directory_next_info(m_directory_, name,
					  capacity, &required, &kind, &error)
				: hx_os_directory_next(m_directory_, name,
					  capacity, &required, &error);

			if (status == HX_OS_OUTPUT_FULL) {
				capacity = required;
				continue;
			}

			if (status != HX_OS_OK)
				return Failure(status, error);

			return Entry{
				std::move(buffer.value()), kind, queryKind};
		}
	}

private:
	hx_os_directory *m_directory_;
	hx_os_context *m_context_;

	template <typename F> struct Visitor {
		F *function;
	};

	template <typename F>
	static Result<void> invokeVisitor(
		F &function, StringView name, std::true_type) noexcept
	{
		static_assert(std::is_pointer<F>::value ||
				std::is_function<F>::value ||
				noexcept(function(name)),
			"Directory callbacks must be noexcept");

		return Result<void>(function(name) ? HX_OS_OK : HX_OS_EOF);
	}

	template <typename F>
	static Result<void> invokeVisitor(
		F &function, StringView name, std::false_type) noexcept
	{
		return detail::InvokeCallback(function, name);
	}

	template <typename F>
	static hx_os_status HX_OS_CALL visit(
		hx_string_view name, void *user, hx_os_error *error) noexcept
	{
		auto &function = *static_cast<Visitor<F> *>(user)->function;

		typedef decltype(function(StringView(name))) Return;

		auto result = invokeVisitor(function, StringView(name),
			std::is_same<Return, bool>());
		*error = result.error();

		return result.status();
	}
};

class Files {
public:
	static Result<hx_os_file_info_extended> info(
		const char *path, bool follow = true) noexcept
	{
		hx_os_file_info_extended value = HX_OS_FILE_INFO_EXTENDED_INIT;
		hx_os_error error = {};
		auto status =
			hx_os_path_stat_extended(path, follow, &value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<String> readText(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_file_read_text(path, out, error);
			},
			context);
	}

	static Result<String> readText(const char *path,
		hx_os_encoding encoding, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_file_read_text_encoded(
					path, encoding, out, error);
			},
			context);
	}

	static Result<void> writeText(
		const char *path, StringView text) noexcept
	{
		size_t invalid = 0;

		auto status = hx_os_text_validate_utf8(
			text.data(), text.size(), &invalid);
		if (status != HX_OS_OK)
			return Failure(status);

		hx_os_error error = {};

		status = hx_os_file_save_atomic(
			path, text.data(), text.size(), &error);

		return detail::Status(status, error);
	}

	static Result<Buffer> load(const char *path,
		size_t maxBytes = 16 * 1024 * 1024,
		hx_os_context *context = NULL) noexcept
	{
		void *data = NULL;
		size_t bytes = 0;
		hx_os_error error = {};

		auto status = hx_os_file_load(
			context, path, maxBytes, &data, &bytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return Buffer::adopt(data, bytes);
	}

	static Result<bool> exists(const char *path) noexcept
	{
		int value = 0;
		hx_os_error error = {};

		auto status = hx_os_file_exists(path, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value != 0;
	}

	static Result<void> copy(
		const char *from, const char *to, bool replace = false) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_file_copy(from, to, replace, &error);

		return detail::Status(status, error);
	}

	static Result<void> removeTree(const char *path) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_directory_remove_recursive(path, &error);

		return detail::Status(status, error);
	}

	static Result<String> currentPath(
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_path_current_string, context);
	}

	static Result<String> absolutePath(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_path_absolute_string(
					path, out, error);
			},
			context);
	}
};
} // namespace hx
#endif
