#include "thread_name.hpp"

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace Util
{
void set_current_thread_name(const char *name)
{
#if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#elif defined(_WIN32) && !defined(_XBOX) && !(defined(WINAPI_FAMILY) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP)
	/* Windows 10 1607 on; looked up, as older systems and headers lack it. */
	typedef HRESULT (WINAPI *set_description_t)(HANDLE, PCWSTR);
	WCHAR wide[64];
	unsigned i;
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	set_description_t set_description = kernel
		? (set_description_t)(void *)GetProcAddress(kernel, "SetThreadDescription") : NULL;
	if (!set_description)
		return;
	for (i = 0; name[i] && i < 63; i++)
		wide[i] = (WCHAR)(unsigned char)name[i];
	wide[i] = 0;
	set_description(GetCurrentThread(), wide);
#else
	(void)name;
#endif
}
}