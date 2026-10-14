#include "power_private.h"
#import <Foundation/Foundation.h>
/**
 * \brief Foundation's public activity API is Objective-C.
 *
 * \details This implementation stays behind the C ABI; disable
 * PERFORMANCE_REQUEST for a C-only adapter.
 */
hx_os_status hx_os_i_activity_set(
	hx_os_i_power *power, int active, hx_os_error *error)
{
	@autoreleasepool {
		@try {
			if (active) {
				NSActivityOptions options =
					NSActivityUserInitiated |
					NSActivityLatencyCritical;
				NSString *reason = (NSString *)power->detail;
				id token = [[NSProcessInfo processInfo]
					beginActivityWithOptions:options
							  reason:reason];

				if (!token)
					return hx_os_i_error(
						error, HX_OS_NO_MEMORY, 0, 0);

				power->native = (uintptr_t)[token retain];
			}
			else {
				id token = (id)power->native;

				@try {
					[[NSProcessInfo processInfo]
						endActivity:token];
				} @finally {
					power->native = 0;
					[token release];
				}
			}

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		} @catch (NSException *exception) {
			(void)exception;

			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		}
	}
}
