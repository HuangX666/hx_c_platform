#include "power_private.h"

#include <dlfcn.h>
#include <unistd.h>

/**
 * \brief Runtime binding to the public sd-bus C ABI.
 *
 * \details Native objects stay opaque; no implementation or SDK header is
 * redistributed. Calls pass NULL for the optional native error object and
 * retain sd-bus's errno-style results.
 */
typedef struct sd_bus hx_os_sd_bus;
typedef struct sd_bus_message hx_os_sd_message;
typedef struct sd_bus_error hx_os_sd_error;

typedef struct hx_os_desktop_power {
	void *library;
	hx_os_sd_bus *bus;
	int (*open_user)(hx_os_sd_bus **);
	void (*close)(hx_os_sd_bus *);
	hx_os_sd_bus *(*unref)(hx_os_sd_bus *);
	int (*new_call)(hx_os_sd_bus *, hx_os_sd_message **, const char *,
		const char *, const char *, const char *);
	int (*append)(hx_os_sd_message *, const char *, ...);
	int (*auto_start)(hx_os_sd_message *, int);
	int (*interactive)(hx_os_sd_message *, int);
	int (*exit_on_disconnect)(hx_os_sd_bus *, int);
	int (*call)(hx_os_sd_bus *, hx_os_sd_message *, uint64_t,
		hx_os_sd_error *, hx_os_sd_message **);
	int (*read)(hx_os_sd_message *, const char *, ...);
	int (*signature)(hx_os_sd_message *, const char *);
	hx_os_sd_message *(*message_unref)(hx_os_sd_message *);
} hx_os_desktop_power;

#if defined(RTLD_NODELETE)
static int hx_os_desktop_symbol(
	void *library, const char *name, void *function, size_t bytes)
{
	void *symbol = dlsym(library, name);

	if (!symbol || bytes != sizeof(symbol))
		return 0;

	memcpy(function, &symbol, bytes);

	return 1;
}

static int hx_os_desktop_bind(hx_os_desktop_power *native)
{
#define HX_OS_SD_BIND(field, symbol)                                           \
	if (!hx_os_desktop_symbol(native->library, symbol, &native->field,     \
		    sizeof(native->field)))                                    \
	return 0

	HX_OS_SD_BIND(open_user, "sd_bus_open_user");
	HX_OS_SD_BIND(close, "sd_bus_close");
	HX_OS_SD_BIND(unref, "sd_bus_unref");
	HX_OS_SD_BIND(new_call, "sd_bus_message_new_method_call");
	HX_OS_SD_BIND(append, "sd_bus_message_append");
	HX_OS_SD_BIND(auto_start, "sd_bus_message_set_auto_start");
	HX_OS_SD_BIND(interactive,
		"sd_bus_message_set_allow_interactive_authorization");
	HX_OS_SD_BIND(exit_on_disconnect, "sd_bus_set_exit_on_disconnect");
	HX_OS_SD_BIND(call, "sd_bus_call");
	HX_OS_SD_BIND(read, "sd_bus_message_read");
	HX_OS_SD_BIND(signature, "sd_bus_message_has_signature");
	HX_OS_SD_BIND(message_unref, "sd_bus_message_unref");
#undef HX_OS_SD_BIND

	return 1;
}
#endif

static hx_os_status hx_os_desktop_status(int code, hx_os_error *error)
{
	if (code == ENOENT || code == ENXIO || code == ENOSYS ||
		code == ENOMEDIUM || code == ECONNREFUSED || code == EBADR)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_ERRNO, code);

	return hx_os_i_errno(error, code);
}

hx_os_status hx_os_i_power_open(hx_os_i_power *power, hx_os_error *error)
{
#if defined(RTLD_NODELETE)
	static const char *const names[] = {
		"libsystemd.so.0", "libelogind.so.0"};
	hx_os_desktop_power candidate = {0}, *native;
	size_t index;

	if (power->kind != HX_OS_I_SLEEP_REQUEST)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
		candidate.library = dlopen(
			names[index], RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
		if (!candidate.library)
			continue;
		if (hx_os_desktop_bind(&candidate))
			break;
		(void)dlclose(candidate.library);
		candidate.library = NULL;
	}
	if (!candidate.library)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	native = hx_os_i_alloc(power->reason.context, sizeof(*native), 0);
	if (!native) {
		(void)dlclose(candidate.library);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	*native = candidate;
	power->detail = native;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)power;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_desktop_address(hx_os_error *error)
{
	const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");
	size_t length = 0, offset = 0;

	if (!address || !*address)
		return HX_OS_OK;
	while (length <= 16384 && address[length])
		++length;
	if (length > 16384)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	while (offset < length) {
		if (length - offset < 5 || memcmp(address + offset, "unix:", 5))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		while (offset < length && address[offset] != ';')
			++offset;
		++offset;
	}

	return HX_OS_OK;
}

static void hx_os_desktop_application(char out[256])
{
	char path[4096];
	ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
	const char *name;
	size_t invalid, bytes;

	strcpy(out, "Application");
	if (length <= 0 || (size_t)length >= sizeof(path) - 1)
		return;

	path[length] = 0;
	name = strrchr(path, '/');
	name = name ? name + 1 : path;
	bytes = strlen(name);
	if (bytes && bytes < 256 &&
		hx_os_text_validate_utf8(name, bytes, &invalid) == HX_OS_OK)
		memcpy(out, name, bytes + 1);
}

static hx_os_status hx_os_desktop_request(hx_os_i_power *power, int gnome,
	const char *destination, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_desktop_power *native = power->detail;
	hx_os_sd_message *request = NULL, *reply = NULL;
	const char *path = gnome ? "/org/gnome/SessionManager"
				 : "/org/freedesktop/PowerManagement/Inhibit";
	const char *interface = gnome
		? "org.gnome.SessionManager"
		: "org.freedesktop.PowerManagement.Inhibit";
	char application[256];
	uint64_t now, remaining;
	uint32_t cookie;
	hx_os_status status;
	int code;

	hx_os_desktop_application(application);
	code = native->new_call(
		native->bus, &request, destination, path, interface, "Inhibit");
	if (code >= 0)
		code = native->auto_start(request, 0);
	if (code >= 0)
		code = native->interactive(request, 0);
	if (code >= 0) {
		if (gnome)
			code = native->append(request, "susu", application,
				(uint32_t)0, hx_string_c_str(&power->reason),
				(uint32_t)4);
		else
			code = native->append(request, "ss", application,
				hx_string_c_str(&power->reason));
	}
	if (code < 0) {
		native->message_unref(request);

		return hx_os_desktop_status(-code, error);
	}

	status = hx_os_monotonic_read(&now, error);
	if (status == HX_OS_OK && now >= deadline)
		status = hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	if (status != HX_OS_OK) {
		native->message_unref(request);

		return status;
	}

	remaining = (deadline - now + 999) / 1000;
	code = native->call(native->bus, request, remaining, NULL, &reply);
	native->message_unref(request);
	if (code >= 0) {
		if (native->signature(reply, "u") <= 0)
			code = -EBADMSG;
		else {
			code = native->read(reply, "u", &cookie);
			if (!code)
				code = -EBADMSG;
		}
	}
	native->message_unref(reply);
	if (code < 0)
		return hx_os_desktop_status(-code, error);

	power->native = cookie;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_power_set(
	hx_os_i_power *power, int active, hx_os_error *error)
{
	hx_os_desktop_power *native = power->detail;
	hx_os_deadline deadline;
	hx_os_status status;
	int code;

	if (!active) {
		if (native->bus) {
			native->close(native->bus);
			native->unref(native->bus);
			native->bus = NULL;
		}
		power->native = 0;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	status = hx_os_desktop_address(error);
	if (status != HX_OS_OK)
		return status;
	deadline = hx_os_deadline_after_ms(1000);
	code = native->open_user(&native->bus);
	if (code >= 0)
		code = native->exit_on_disconnect(native->bus, 0);
	if (code < 0)
		status = hx_os_desktop_status(-code, error);
	else {
		status = hx_os_desktop_request(power, 0,
			"org.freedesktop.PowerManagement", deadline, error);
		if (status == HX_OS_NOT_SUPPORTED)
			status = hx_os_desktop_request(power, 0,
				"org.freedesktop.PowerManagement.Inhibit",
				deadline, error);
		if (status == HX_OS_NOT_SUPPORTED)
			status = hx_os_desktop_request(power, 1,
				"org.gnome.SessionManager", deadline, error);
	}
	if (status != HX_OS_OK)
		(void)hx_os_i_power_set(power, 0, NULL);

	return status;
}

hx_os_status hx_os_i_power_dispose(hx_os_i_power *power, hx_os_error *error)
{
	hx_os_desktop_power *native = power->detail;

	if (native) {
		(void)hx_os_i_power_set(power, 0, NULL);
		(void)dlclose(native->library);
		hx_os_memory_free(native);
		power->detail = NULL;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
