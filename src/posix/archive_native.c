#include "archive_private.h"
#include "file_private.h"

#if HX_OS_HAVE_ARCHIVE
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <locale.h>
#include <pthread.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

struct hx_ar_root {
	hx_ar_job *job;
	hx_string path;
	int descriptor;
};

struct hx_ar_sink {
	hx_ar_root *root;
	hx_os_file *file;
	char *name;
	char temporary[80];
	int parent;
	int published;
};

static hx_os_atomic hx_ar_serial;

#if HX_OS_HAVE_LOCAL_PRINTF
/* Concurrent operations share one immutable locale and retire its lazy
 * conversion state only after the last operation leaves. No lock spans I/O,
 * a codec call, or a caller callback; no locale survives an idle interval. */
static pthread_mutex_t hx_ar_locale_mutex = PTHREAD_MUTEX_INITIALIZER;
static locale_t hx_ar_locale;
static size_t hx_ar_locale_users;

hx_os_status hx_ar_i_locale_begin(hx_ar_job *job)
{
	int code = pthread_mutex_lock(&hx_ar_locale_mutex);
	hx_os_status status = HX_OS_OK;
	hx_os_error error = {0};

	if (code) {
		status = hx_os_i_errno(&error, code);
		return hx_ar_i_fail(
			job, status, &error, "Archive locale lifetime");
	}
	if (!hx_ar_locale) {
		hx_ar_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
		if (!hx_ar_locale)
			hx_ar_locale = newlocale(
				LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
	}
	if (hx_ar_locale) {
		job->previous_locale = (void *)uselocale(hx_ar_locale);
		if (job->previous_locale) {
			job->locale = (void *)hx_ar_locale;
			job->locale_thread = hx_os_thread_id();
			++hx_ar_locale_users;
		}
		else if (!hx_ar_locale_users) {
			freelocale(hx_ar_locale);
			hx_ar_locale = (locale_t)0;
		}
	}
	(void)pthread_mutex_unlock(&hx_ar_locale_mutex);
	if (!job->locale)
		return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
			"libarchive needs an available per-thread UTF-8 "
			"locale");
	return status;
}

void hx_ar_i_locale_end(hx_ar_job *job)
{
	if (job->locale) {
		(void)uselocale((locale_t)job->previous_locale);
		(void)pthread_mutex_lock(&hx_ar_locale_mutex);
		if (!--hx_ar_locale_users) {
			freelocale(hx_ar_locale);
			hx_ar_locale = (locale_t)0;
		}
		(void)pthread_mutex_unlock(&hx_ar_locale_mutex);
		job->locale = NULL;
	}
}

void *hx_ar_i_callback_begin(hx_ar_job *job)
{
	if (job->locale && job->locale_thread == hx_os_thread_id())
		return (void *)uselocale((locale_t)job->previous_locale);
	return NULL;
}

void hx_ar_i_callback_end(void *previous)
{
	if (previous)
		(void)uselocale((locale_t)previous);
}

#else
hx_os_status hx_ar_i_locale_begin(hx_ar_job *job)
{
	return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
		"Per-thread locales are unavailable on this target");
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
#endif

static hx_os_status hx_ar_errno(hx_ar_job *job, int code, const char *message)
{
	hx_os_error error = {0};
	hx_os_status status = hx_os_i_errno(&error, code);

	return hx_ar_i_fail(job, status, &error, message);
}

static int hx_ar_openat(int parent, const char *path, int flags, mode_t mode)
{
	int descriptor;

	do {
		descriptor = openat(
			parent, path, flags | O_CLOEXEC | O_NOFOLLOW, mode);
	} while (descriptor < 0 && errno == EINTR);

	return descriptor;
}

hx_os_status hx_ar_i_root_open(
	hx_ar_job *job, const char *path, int create, hx_ar_root **out)
{
	hx_ar_root *root;
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	int descriptor;

	*out = NULL;
	if (create)
		status = hx_os_directory_create(path, 1, &error);
	if (status != HX_OS_OK && status != HX_OS_EXISTS)
		return hx_ar_i_fail(job, status, &error, "Create archive root");
	descriptor = hx_ar_openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
	if (descriptor < 0)
		return hx_ar_errno(job, errno, "Open archive directory anchor");
	root = hx_os_i_zero(job->context, sizeof(*root));
	if (!root) {
		(void)close(descriptor);
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	}
	root->job = job;
	root->descriptor = descriptor;
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
		(void)close(root->descriptor);
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
#if defined(O_PATH)
	/* Ancestor identity needs search permission, not directory listing. */
	const int flags = O_PATH | O_DIRECTORY;
#else
	const int flags = O_RDONLY | O_DIRECTORY;
#endif
	struct stat target, current, parent;
	int descriptor = hx_ar_openat(AT_FDCWD, path, flags, 0);
	int next, code = 0;
	size_t depth = 0;

	*inside = 0;
	if (descriptor < 0)
		return hx_ar_errno(
			root->job, errno, "Open packing output parent");
	if (fstat(root->descriptor, &target))
		code = errno;
	while (!code) {
		if (fstat(descriptor, &current)) {
			code = errno;
			break;
		}
		if (target.st_dev == current.st_dev &&
			target.st_ino == current.st_ino) {
			*inside = 1;
			break;
		}
		next = hx_ar_openat(descriptor, "..", flags, 0);
		if (next < 0) {
			code = errno;
			break;
		}
		if (fstat(next, &parent))
			code = errno;
		(void)close(descriptor);
		descriptor = next;
		if (code ||
			(current.st_dev == parent.st_dev &&
				current.st_ino == parent.st_ino))
			break;
		if (++depth == 1024) {
			(void)close(descriptor);
			return hx_ar_i_fail(root->job, HX_OS_LIMIT, NULL,
				"Filesystem ancestor depth");
		}
	}
	(void)close(descriptor);
	if (code)
		return hx_ar_errno(
			root->job, code, "Inspect packing output parent");

	return HX_OS_OK;
}

/* path was validated by the common layer; every parent is nevertheless
 * opened relative to its pinned predecessor with O_NOFOLLOW. */
static hx_os_status hx_ar_parent(
	hx_ar_root *root, const char *path, int create, int *out, char **leaf)
{
	char *copy = hx_os_i_alloc(root->job->context, strlen(path) + 1, 0);
	char *part, *slash;
	int descriptor = -1, next, code = 0;

	*out = -1;
	*leaf = NULL;
	if (!copy)
		return hx_ar_i_fail(root->job, HX_OS_NO_MEMORY, NULL, NULL);
	strcpy(copy, path);
	descriptor =
		hx_ar_openat(root->descriptor, ".", O_RDONLY | O_DIRECTORY, 0);
	if (descriptor < 0)
		code = errno;
	part = copy;
	while (!code && (slash = strchr(part, '/')) != NULL) {
		*slash = 0;
		if (create && mkdirat(descriptor, part, 0700) &&
			errno != EEXIST) {
			code = errno;
			break;
		}
		next = hx_ar_openat(
			descriptor, part, O_RDONLY | O_DIRECTORY, 0);
		if (next < 0) {
			code = errno;
			break;
		}
		(void)close(descriptor);
		descriptor = next;
		part = slash + 1;
	}
	if (code) {
		if (descriptor >= 0)
			(void)close(descriptor);
		hx_os_memory_free(copy);
		return hx_ar_errno(root->job, code, "Open archive path parent");
	}
	memmove(copy, part, strlen(part) + 1);
	*out = descriptor;
	*leaf = copy;

	return HX_OS_OK;
}

hx_os_status hx_ar_i_directory(hx_ar_root *root, const char *path)
{
	char *name;
	int parent, descriptor, code = 0;
	hx_os_status status = hx_ar_parent(root, path, 1, &parent, &name);

	if (status != HX_OS_OK)
		return status;
	if (mkdirat(parent, name, 0700) && errno != EEXIST)
		code = errno;
	if (!code) {
		descriptor =
			hx_ar_openat(parent, name, O_RDONLY | O_DIRECTORY, 0);
		if (descriptor < 0)
			code = errno;
		else
			(void)close(descriptor);
	}
	(void)close(parent);
	hx_os_memory_free(name);

	return code ? hx_ar_errno(root->job, code, "Create archive directory")
		    : HX_OS_OK;
}

hx_os_status hx_ar_i_input(hx_ar_root *root, const char *path, hx_os_file **out)
{
	char *name;
	int parent, descriptor, code;
	struct stat info;
	hx_os_file *file;
	hx_os_status status = hx_ar_parent(root, path, 0, &parent, &name);

	*out = NULL;
	if (status != HX_OS_OK)
		return status;
	descriptor = hx_ar_openat(parent, name, O_RDONLY | O_NONBLOCK, 0);
	code = descriptor < 0 ? errno : 0;
	(void)close(parent);
	hx_os_memory_free(name);
	if (code)
		return hx_ar_errno(root->job, code, "Open packing input");
	if (fstat(descriptor, &info)) {
		code = errno;
		(void)close(descriptor);
		return hx_ar_errno(root->job, code, "Inspect packing input");
	}
	if (!S_ISREG(info.st_mode)) {
		(void)close(descriptor);
		return hx_ar_i_fail(root->job, HX_OS_NOT_SUPPORTED, NULL,
			"Packing input is not a regular file");
	}
	file = hx_os_i_zero(root->job->context, sizeof(*file));
	if (!file) {
		(void)close(descriptor);
		return hx_ar_i_fail(root->job, HX_OS_NO_MEMORY, NULL, NULL);
	}
	file->descriptor = descriptor;
	file->flags = HX_OS_FILE_READ;
	*out = file;

	return HX_OS_OK;
}

hx_os_status hx_ar_i_sink(hx_ar_root *root, const char *path, int encrypted,
	hx_ar_sink **out, hx_os_file **file)
{
	hx_ar_sink *sink = hx_os_i_zero(root->job->context, sizeof(*sink));
	hx_os_status status;
	int descriptor = -1, attempt;
	hx_os_error error = {0};

	*out = NULL;
	*file = NULL;
	if (!sink)
		return hx_ar_i_fail(root->job, HX_OS_NO_MEMORY, NULL, NULL);
	sink->root = root;
	sink->parent = -1;
	status = hx_ar_parent(root, path, 1, &sink->parent, &sink->name);
	if (status != HX_OS_OK)
		goto failed;
	for (attempt = 0; attempt < 32; ++attempt) {
		uint64_t serial = (uint64_t)hx_os_atomic_fetch_add(
			&hx_ar_serial, 1, HX_OS_RELAXED);

		(void)snprintf(sink->temporary, sizeof(sink->temporary),
			".hx-archive-%lx-%llx.tmp", (unsigned long)getpid(),
			(unsigned long long)serial);
		if (!strcmp(sink->temporary, sink->name))
			continue;
		descriptor = hx_ar_openat(sink->parent, sink->temporary,
			O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (descriptor >= 0 || errno != EEXIST)
			break;
	}
	if (descriptor < 0) {
		status = hx_ar_errno(
			root->job, errno, "Create extraction temporary");
		/* Never remove a colliding file owned by someone else. */
		sink->temporary[0] = 0;
		goto failed;
	}
	sink->file = hx_os_i_zero(root->job->context, sizeof(*sink->file));
	if (!sink->file) {
		(void)close(descriptor);
		status = hx_ar_i_fail(root->job, HX_OS_NO_MEMORY, NULL, NULL);
		goto failed;
	}
	sink->file->descriptor = descriptor;
	sink->file->flags = HX_OS_FILE_WRITE;
	if (encrypted) {
		status = hx_os_i_file_check_private(sink->file, &error);
		if (status != HX_OS_OK) {
			(void)hx_ar_i_fail(root->job, status, &error,
				"Private plaintext staging is unavailable");
			goto failed;
		}
	}
	*out = sink;
	*file = sink->file;

	return HX_OS_OK;
failed:
	hx_ar_i_sink_free(&sink);
	return status;
}

hx_os_status hx_ar_i_publish(hx_ar_sink *sink)
{
	hx_ar_job *job = sink->root->job;
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	struct stat info;
	int code = 0, moved = 0;

	if (job->options.flags & HX_OS_ARCHIVE_SYNC_FILES)
		status = hx_os_file_sync(sink->file, &error);
	if (status == HX_OS_OK)
		status = hx_os_file_close(&sink->file, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(
			job, status, &error, "Finish extraction temporary");
	if (job->options.flags & HX_OS_ARCHIVE_OVERWRITE) {
		if (!fstatat(sink->parent, sink->name, &info,
			    AT_SYMLINK_NOFOLLOW)) {
			if (!S_ISREG(info.st_mode))
				return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED,
					NULL,
					"Only regular destination files may be "
					"replaced");
		}
		else if (errno != ENOENT)
			code = errno;
		if (!code &&
			renameat(sink->parent, sink->temporary, sink->parent,
				sink->name))
			code = errno;
	}
	else {
#if defined(__linux__) && defined(SYS_renameat2)
		if (!syscall(SYS_renameat2, sink->parent, sink->temporary,
			    sink->parent, sink->name, 1u))
			moved = 1;
		else if (errno != ENOSYS && errno != EINVAL &&
			errno != EOPNOTSUPP)
			code = errno;
#elif defined(__APPLE__) && defined(RENAME_EXCL)
		if (!renameatx_np(sink->parent, sink->temporary, sink->parent,
			    sink->name, RENAME_EXCL))
			moved = 1;
		else if (errno != ENOTSUP && errno != EINVAL)
			code = errno;
#endif
		if (!code && !moved &&
			linkat(sink->parent, sink->temporary, sink->parent,
				sink->name, 0))
			code = errno;
	}
	if (code)
		return hx_ar_errno(job, code, "Publish extracted file");
	sink->published = 1;
	(void)hx_os_mutex_lock(job->mutex, NULL);
	++job->result.published;
	(void)hx_os_mutex_unlock(job->mutex, NULL);
	if (!moved && !(job->options.flags & HX_OS_ARCHIVE_OVERWRITE) &&
		unlinkat(sink->parent, sink->temporary, 0))
		return hx_ar_errno(
			job, errno, "Remove extraction staging name");
	sink->temporary[0] = 0;
	if ((job->options.flags & HX_OS_ARCHIVE_SYNC_FILES) &&
		fsync(sink->parent))
		return hx_ar_errno(job, errno, "Sync extraction directory");

	return HX_OS_OK;
}

void hx_ar_i_sink_free(hx_ar_sink **owner)
{
	hx_ar_sink *sink = *owner;

	if (!sink)
		return;
	(void)hx_os_file_close(&sink->file, NULL);
	if (sink->temporary[0] && sink->parent >= 0)
		(void)unlinkat(sink->parent, sink->temporary, 0);
	if (sink->parent >= 0)
		(void)close(sink->parent);
	hx_os_memory_free(sink->name);
	hx_os_memory_free(sink);
	*owner = NULL;
}
#endif
