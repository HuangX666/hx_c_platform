#include "archive_private.h"
#include "file_private.h"

#if HX_OS_HAVE_ARCHIVE

hx_os_status hx_ar_i_locale_begin(hx_ar_job *job)
{
	(void)job;
	return HX_OS_OK;
}

void hx_ar_i_locale_end(hx_ar_job *job)
{
	(void)job;
}

void *hx_ar_i_callback_begin(hx_ar_job *job)
{
	(void)job;
	return NULL;
}

void hx_ar_i_callback_end(void *previous)
{
	(void)previous;
}

#include <stdio.h>

typedef struct hx_ar_parent {
	hx_string path;
	HANDLE *handles;
	size_t count;
} hx_ar_parent;

struct hx_ar_root {
	hx_ar_job *job;
	hx_string path;
	HANDLE handle;
};

struct hx_ar_sink {
	hx_ar_root *root;
	hx_ar_parent parent;
	hx_os_file *file;
	hx_string target, temporary;
	int owns_temporary;
};

static hx_os_atomic hx_ar_serial;

static hx_os_status hx_ar_windows(
	hx_ar_job *job, DWORD code, const char *message)
{
	hx_os_error error = {0};
	hx_os_status status = hx_os_i_win32(&error, code);

	return hx_ar_i_fail(job, status, &error, message);
}

static hx_os_status hx_ar_open_directory(
	hx_ar_job *job, const char *path, HANDLE *out)
{
	hx_os_error error = {0};
	wchar_t *wide = hx_os_i_windows_path(job->context, path, &error);
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE handle;
	DWORD code;

	*out = INVALID_HANDLE_VALUE;
	if (!wide)
		return hx_ar_i_fail(job, error.status, &error, NULL);
	/* Deny ancestor rename/deletion. Sharing write is needed by child
	 * publication; the caller excludes concurrent reparse-point mutation.
	 */
	handle = CreateFileW(wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
		NULL);
	code = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(wide);
	if (code)
		return hx_ar_windows(
			job, code, "Open archive directory anchor");
	if (!GetFileInformationByHandle(handle, &info)) {
		code = GetLastError();
		(void)CloseHandle(handle);
		return hx_ar_windows(
			job, code, "Inspect archive directory anchor");
	}
	if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		(void)CloseHandle(handle);
		return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
			"Archive directories cannot be links or reparse "
			"points");
	}
	*out = handle;

	return HX_OS_OK;
}

hx_os_status hx_ar_i_root_open(
	hx_ar_job *job, const char *path, int create, hx_ar_root **out)
{
	hx_ar_root *root;
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	HANDLE handle;

	*out = NULL;
	if (create)
		status = hx_os_directory_create(path, 1, &error);
	if (status != HX_OS_OK && status != HX_OS_EXISTS)
		return hx_ar_i_fail(job, status, &error, "Create archive root");
	status = hx_ar_open_directory(job, path, &handle);
	if (status != HX_OS_OK)
		return status;
	root = hx_os_i_zero(job->context, sizeof(*root));
	if (!root) {
		(void)CloseHandle(handle);
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	}
	root->job = job;
	root->handle = handle;
	hx_string_init(&root->path, job->context);
	status = hx_os_i_path_absolute(path, &root->path, &error);
	if (status != HX_OS_OK) {
		hx_ar_i_root_close(root);
		return hx_ar_i_fail(
			job, status, &error, "Resolve archive root");
	}
	*out = root;

	return HX_OS_OK;
}

void hx_ar_i_root_close(hx_ar_root *root)
{
	if (root) {
		(void)CloseHandle(root->handle);
		hx_string_destroy(&root->path);
		hx_os_memory_free(root);
	}
}

const char *hx_ar_i_root_path(const hx_ar_root *root)
{
	return hx_string_c_str(&root->path);
}

hx_os_status hx_ar_i_contains(hx_ar_root *root, const char *path, int *inside)
{
	hx_string current = HX_STRING_INIT;
	BY_HANDLE_FILE_INFORMATION target, info;
	hx_os_error error = {0};
	hx_os_status status;
	size_t depth;

	*inside = 0;
	if (!GetFileInformationByHandle(root->handle, &target))
		return hx_ar_windows(
			root->job, GetLastError(), "Inspect source root");
	hx_string_init(&current, root->job->context);
	status = hx_os_i_path_absolute(path, &current, &error);
	for (depth = 0; status == HX_OS_OK && depth < 1024; ++depth) {
		HANDLE handle;
		hx_string_view parent;

		status = hx_ar_open_directory(
			root->job, hx_string_c_str(&current), &handle);
		if (status != HX_OS_OK)
			break;
		if (!GetFileInformationByHandle(handle, &info))
			status = hx_ar_windows(root->job, GetLastError(),
				"Inspect packing output parent");
		(void)CloseHandle(handle);
		if (status != HX_OS_OK)
			break;
		if (target.dwVolumeSerialNumber == info.dwVolumeSerialNumber &&
			target.nFileIndexHigh == info.nFileIndexHigh &&
			target.nFileIndexLow == info.nFileIndexLow) {
			*inside = 1;
			break;
		}
		status = hx_os_path_dirname(
			hx_string_c_str(&current), &parent, &error);
		if (status != HX_OS_OK || parent.size == current.size)
			break;
		status = hx_string_erase(&current, parent.size,
			current.size - parent.size, &error);
	}
	if (status == HX_OS_OK && depth == 1024)
		status = hx_ar_i_fail(root->job, HX_OS_LIMIT, NULL,
			"Filesystem ancestor depth");
	hx_string_destroy(&current);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(root->job, status,
			error.status == status ? &error : NULL,
			"Packing output parent");

	return status;
}

static void hx_ar_parent_free(hx_ar_parent *parent)
{
	while (parent->count)
		(void)CloseHandle(parent->handles[--parent->count]);
	hx_os_memory_free(parent->handles);
	hx_string_destroy(&parent->path);
}

static hx_os_status hx_ar_parent_open(hx_ar_root *root, const char *path,
	int create, int directory, hx_ar_parent *parent)
{
	hx_ar_job *job = root->job;
	hx_os_error error = {0};
	hx_os_status status;
	size_t start = 0, i, bytes = strlen(path), count = 0;

	memset(parent, 0, sizeof(*parent));
	hx_string_init(&parent->path, job->context);
	for (i = 0; i < bytes; ++i)
		count += path[i] == '/';
	count += (size_t)directory;
	parent->handles = count ? hx_os_i_alloc(job->context,
					  count * sizeof(*parent->handles), 0)
				: NULL;
	if (count && !parent->handles)
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	status = hx_string_assign(
		&parent->path, hx_string_view_of(&root->path), &error);
	for (i = 0; status == HX_OS_OK && i <= bytes; ++i) {
		hx_string_view component;

		if (path[i] != '/' && (i != bytes || !directory))
			continue;
		component.data = path + start;
		component.size = i - start;
		status = hx_string_append_cstr(&parent->path, "/", &error);
		if (status == HX_OS_OK)
			status = hx_string_append(
				&parent->path, component, &error);
		if (status == HX_OS_OK && create) {
			status = hx_os_directory_create(
				hx_string_c_str(&parent->path), 0, &error);
			if (status == HX_OS_EXISTS)
				status = HX_OS_OK;
		}
		if (status == HX_OS_OK) {
			status = hx_ar_open_directory(job,
				hx_string_c_str(&parent->path),
				parent->handles + parent->count);
			if (status == HX_OS_OK)
				++parent->count;
		}
		start = i + 1;
	}
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status,
			status == error.status ? &error : NULL,
			"Archive path parent");

	return status;
}

hx_os_status hx_ar_i_directory(hx_ar_root *root, const char *path)
{
	hx_ar_parent parent;
	hx_os_status status = hx_ar_parent_open(root, path, 1, 1, &parent);

	hx_ar_parent_free(&parent);

	return status;
}

static hx_os_status hx_ar_full(
	hx_ar_root *root, const char *path, hx_string *full)
{
	hx_os_error error = {0};
	hx_os_status status =
		hx_string_assign(full, hx_string_view_of(&root->path), &error);

	if (status == HX_OS_OK)
		status = hx_string_append_cstr(full, "/", &error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(full, path, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(root->job, status, &error, NULL);

	return status;
}

hx_os_status hx_ar_i_input(hx_ar_root *root, const char *path, hx_os_file **out)
{
	hx_ar_parent parent;
	hx_string full = HX_STRING_INIT;
	hx_os_status status;
	hx_os_error error = {0};
	wchar_t *wide = NULL;
	HANDLE handle = INVALID_HANDLE_VALUE;
	BY_HANDLE_FILE_INFORMATION info;
	hx_os_file *file;

	*out = NULL;
	hx_string_init(&full, root->job->context);
	status = hx_ar_parent_open(root, path, 0, 0, &parent);
	if (status == HX_OS_OK)
		status = hx_ar_full(root, path, &full);
	if (status == HX_OS_OK) {
		wide = hx_os_i_windows_path(
			root->job->context, hx_string_c_str(&full), &error);
		if (!wide)
			status = hx_ar_i_fail(
				root->job, error.status, &error, NULL);
	}
	if (status == HX_OS_OK) {
		handle = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT |
				FILE_FLAG_SEQUENTIAL_SCAN,
			NULL);
		if (handle == INVALID_HANDLE_VALUE)
			status = hx_ar_windows(root->job, GetLastError(),
				"Open packing input");
	}
	if (status == HX_OS_OK) {
		if (!GetFileInformationByHandle(handle, &info))
			status = hx_ar_windows(root->job, GetLastError(),
				"Inspect packing input");
		else if (GetFileType(handle) != FILE_TYPE_DISK ||
			(info.dwFileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY |
					FILE_ATTRIBUTE_REPARSE_POINT |
					FILE_ATTRIBUTE_DEVICE)))
			status = hx_ar_i_fail(root->job, HX_OS_NOT_SUPPORTED,
				NULL, "Packing input is not a regular file");
	}
	if (status == HX_OS_OK) {
		file = hx_os_i_zero(root->job->context, sizeof(*file));
		if (!file)
			status = hx_ar_i_fail(
				root->job, HX_OS_NO_MEMORY, NULL, NULL);
		else {
			file->handle = handle;
			file->flags = HX_OS_FILE_READ;
			*out = file;
			handle = INVALID_HANDLE_VALUE;
		}
	}
	if (handle != INVALID_HANDLE_VALUE)
		(void)CloseHandle(handle);
	hx_os_memory_free(wide);
	hx_string_destroy(&full);
	hx_ar_parent_free(&parent);

	return status;
}

hx_os_status hx_ar_i_sink(hx_ar_root *root, const char *path, int encrypted,
	hx_ar_sink **out, hx_os_file **file)
{
	hx_ar_job *job = root->job;
	hx_ar_sink *sink = hx_os_i_zero(job->context, sizeof(*sink));
	hx_os_status status;
	hx_os_error error = {0};
	int attempt;

	*out = NULL;
	*file = NULL;
	if (!sink)
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	sink->root = root;
	hx_string_init(&sink->target, job->context);
	hx_string_init(&sink->temporary, job->context);
	status = hx_ar_parent_open(root, path, 1, 0, &sink->parent);
	if (status == HX_OS_OK)
		status = hx_ar_full(root, path, &sink->target);
	for (attempt = 0; status == HX_OS_OK && attempt < 32; ++attempt) {
		char name[80];
		uint64_t serial = (uint64_t)hx_os_atomic_fetch_add(
			&hx_ar_serial, 1, HX_OS_RELAXED);
		const char *leaf = strrchr(path, '/');
		hx_string_view view;

		(void)snprintf(name, sizeof(name), ".hx-archive-%lx-%llx.tmp",
			(unsigned long)GetCurrentProcessId(),
			(unsigned long long)serial);
		view.data = leaf ? leaf + 1 : path;
		view.size = strlen(view.data);
		status = hx_string_assign(&sink->temporary, view, &error);
		if (status != HX_OS_OK)
			break;
		if (!hx_string_compare_ascii_nocase(
			    &sink->temporary, hx_string_view_from_cstr(name)))
			continue;
		status = hx_string_assign(&sink->temporary,
			hx_string_view_of(&sink->parent.path), &error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(
				&sink->temporary, "/", &error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(
				&sink->temporary, name, &error);
		if (status != HX_OS_OK)
			break;
		status = encrypted
			? hx_os_i_file_create_private(job->context,
				  hx_string_c_str(&sink->temporary),
				  &sink->file, &error)
			: hx_os_file_open(job->context,
				  hx_string_c_str(&sink->temporary),
				  HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
					  HX_OS_FILE_EXCLUSIVE,
				  &sink->file, &error);
		if (status == HX_OS_OK) {
			sink->owns_temporary = 1;
			break;
		}
		if (status != HX_OS_EXISTS)
			break;
		status = HX_OS_OK;
	}
	if (status == HX_OS_OK && !sink->file)
		status = hx_os_i_error(&error, HX_OS_EXISTS, 0, 0);
	if (status != HX_OS_OK) {
		(void)hx_ar_i_fail(job, status,
			status == error.status ? &error : NULL,
			"Create extraction temporary");
		hx_ar_i_sink_free(&sink);

		return status;
	}
	*out = sink;
	*file = sink->file;

	return HX_OS_OK;
}

hx_os_status hx_ar_i_publish(hx_ar_sink *sink)
{
	hx_ar_job *job = sink->root->job;
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	hx_os_file_info info;
	int replace = (job->options.flags & HX_OS_ARCHIVE_OVERWRITE) != 0;

	if (job->options.flags & HX_OS_ARCHIVE_SYNC_FILES)
		status = hx_os_file_sync(sink->file, &error);
	if (status == HX_OS_OK)
		status = hx_os_file_close(&sink->file, &error);
	if (status == HX_OS_OK && replace) {
		status = hx_os_path_stat(
			hx_string_c_str(&sink->target), 0, &info, &error);
		if (status == HX_OS_NOT_FOUND)
			status = HX_OS_OK;
		else if (status == HX_OS_OK && info.kind != HX_OS_FILE_REGULAR)
			status = hx_os_i_error(
				&error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	if (status == HX_OS_OK)
		status = hx_os_file_move(hx_string_c_str(&sink->temporary),
			hx_string_c_str(&sink->target), replace, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(
			job, status, &error, "Publish extracted file");
	sink->owns_temporary = 0;
	(void)hx_os_mutex_lock(job->mutex, NULL);
	++job->result.published;
	(void)hx_os_mutex_unlock(job->mutex, NULL);

	return HX_OS_OK;
}

void hx_ar_i_sink_free(hx_ar_sink **owner)
{
	hx_ar_sink *sink = *owner;

	if (!sink)
		return;
	(void)hx_os_file_close(&sink->file, NULL);
	if (sink->owns_temporary)
		(void)hx_os_file_remove(
			hx_string_c_str(&sink->temporary), NULL);
	hx_string_destroy(&sink->temporary);
	hx_string_destroy(&sink->target);
	hx_ar_parent_free(&sink->parent);
	hx_os_memory_free(sink);
	*owner = NULL;
}
#endif
