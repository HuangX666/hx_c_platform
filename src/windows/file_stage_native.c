#include "file_private.h"
#include "file_utils_private.h"

hx_os_status hx_os_i_file_publish(const char *temporary, const char *path,
	int replace, hx_os_error *error)
{
	return hx_os_file_move(temporary, path, replace, error);
}

hx_os_status hx_os_i_file_create_private(hx_os_context *context,
	const char *path, hx_os_file **out, hx_os_error *error)
{
	union {
		TOKEN_USER alignment;
		unsigned char bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
	} user;

	union {
		ACL alignment;
		unsigned char bytes[sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) +
			SECURITY_MAX_SID_SIZE];
	} acl;

	SECURITY_DESCRIPTOR descriptor;
	SECURITY_ATTRIBUTES attributes = {
		sizeof(attributes), &descriptor, FALSE};
	HANDLE token = NULL, handle = INVALID_HANDLE_VALUE;
	hx_os_file *file = NULL;
	wchar_t *wide = NULL;
	DWORD bytes, flags = 0, code = 0;
	hx_os_error local;

	*out = NULL;
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(context, path, error);
	if (!wide)
		return error->status;
	file = hx_os_i_zero(context, sizeof(*file));
	if (!file) {
		hx_os_memory_free(wide);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
		code = GetLastError();
		if (code == ERROR_NO_TOKEN) {
			code = OpenProcessToken(
				       GetCurrentProcess(), TOKEN_QUERY, &token)
				? 0
				: GetLastError();
		}
	}
	if (!code &&
		(!GetTokenInformation(
			 token, TokenUser, &user, sizeof(user), &bytes) ||
			!InitializeSecurityDescriptor(
				&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
			!InitializeAcl(
				&acl.alignment, sizeof(acl), ACL_REVISION) ||
			!AddAccessAllowedAce(&acl.alignment, ACL_REVISION,
				FILE_ALL_ACCESS, user.alignment.User.Sid) ||
			!SetSecurityDescriptorOwner(
				&descriptor, user.alignment.User.Sid, FALSE) ||
			!SetSecurityDescriptorDacl(
				&descriptor, TRUE, &acl.alignment, FALSE) ||
			!SetSecurityDescriptorControl(&descriptor,
				SE_DACL_PROTECTED, SE_DACL_PROTECTED)))
		code = GetLastError();
	if (!code) {
		handle = CreateFileW(wide, GENERIC_WRITE | FILE_READ_ATTRIBUTES,
			0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (handle == INVALID_HANDLE_VALUE)
			code = GetLastError();
	}
	if (!code &&
		!GetVolumeInformationByHandleW(
			handle, NULL, 0, NULL, NULL, &flags, NULL, 0))
		code = GetLastError();
	if (!code && !(flags & FILE_PERSISTENT_ACLS))
		code = ERROR_NOT_SUPPORTED;
	if (token)
		(void)CloseHandle(token);
	if (code && handle != INVALID_HANDLE_VALUE) {
		(void)CloseHandle(handle);
		(void)DeleteFileW(wide);
	}
	hx_os_memory_free(wide);
	if (code) {
		hx_os_memory_free(file);

		return hx_os_i_win32(error, code);
	}
	file->handle = handle;
	file->flags = HX_OS_FILE_WRITE;
	*out = file;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
