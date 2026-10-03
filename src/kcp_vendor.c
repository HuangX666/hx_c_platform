/* Upstream files remain byte-for-byte intact. Only private symbol names and
 * allocation routing are supplied by this translation unit's integration.
 */
#include <stdlib.h>
#include "kcp_symbols.h"

void *hx_os_i_kcp_allocate(size_t bytes);
void hx_os_i_kcp_deallocate(void *memory);

#define malloc hx_os_i_kcp_allocate
#define free hx_os_i_kcp_deallocate
#include <ikcp.c>
