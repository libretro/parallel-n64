#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cwchar>
#include "Log.h"
#include "PluginAPI.h"
#include "wst.h"

void LOG(u16 type, const char * format, ...) {
	if (type > LOG_LEVEL)
		return;

	wchar_t logPath[PLUGIN_PATH_SIZE + 16];
	api().GetUserDataPath(logPath);
	gln_wcscat(logPath, wst("/gliden64.log"));

#ifdef OS_WINDOWS
	FILE *dumpFile = _wfopen(logPath, wst("a+"));
#else
	constexpr size_t bufSize = PLUGIN_PATH_SIZE * 6;
	char cbuf[bufSize];
	wcstombs(cbuf, logPath, bufSize);
	FILE *dumpFile = fopen(cbuf, "a+");
#endif //OS_WINDOWS

	if (dumpFile == nullptr)
		return;
	va_list va;
	va_start(va, format);
	vfprintf(dumpFile, format, va);
	va_end(va);
	/* Most callers terminate their format string with '\n', but a
	 * persistent ~39/92 do not, which under the default LOG_LEVEL
	 * (LOG_WARNING) makes the log file run consecutive entries
	 * together on the same line. Appending a newline only when the
	 * format string did not already end in one keeps the well-
	 * formed callers unchanged and reflows the malformed ones into
	 * their own log lines. */
	{
		size_t fmt_len = strlen(format);
		if (fmt_len == 0 || format[fmt_len - 1] != '\n')
			fputc('\n', dumpFile);
	}
	fclose(dumpFile);
}

#if defined(OS_WINDOWS) && !defined(MINGW)
#include "windows/GLideN64_windows.h"
void debugPrint(const char * format, ...) {
	char text[256];
	wchar_t wtext[256];
	va_list va;
	va_start(va, format);
	vsprintf(text, format, va);
	mbstowcs(wtext, text, 256);
	OutputDebugString(wtext);
	va_end(va);
}
#endif
