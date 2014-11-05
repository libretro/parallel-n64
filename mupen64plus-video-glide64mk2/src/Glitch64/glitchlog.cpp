#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#endif // _WIN32

#include "glitchlog.h"

void display_warning(const char *text, ...)
{
  static int first_message = 100;
  if (first_message)
  {
    char buf[1000];

    va_list ap;

    va_start(ap, text);
    vsprintf(buf, text, ap);
    va_end(ap);
    first_message--;
  }
}

#ifdef _WIN32
void display_error(void)
{
  LPVOID lpMsgBuf;
  if (!FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER |
    FORMAT_MESSAGE_FROM_SYSTEM |
    FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    GetLastError(),
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
    (LPTSTR) &lpMsgBuf,
    0,
    NULL ))
  {
    // Handle the error.
    return;
  }
  // Process any inserts in lpMsgBuf.
  // ...
  // Display the string.
  MessageBox( NULL, (LPCTSTR)lpMsgBuf, "Error", MB_OK | MB_ICONINFORMATION );

  // Free the buffer.
  LocalFree( lpMsgBuf );
}
#endif // _WIN32

#ifdef LOGGING
char out_buf[256];
bool log_open = false;
std::ofstream log_file;

void OPEN_LOG(void)
{
  if (!log_open)
  {
    log_file.open ("wrapper_log.txt", std::ios_base::out|std::ios_base::app);
    log_open = true;
  }
}

void CLOSE_LOG(void)
{
  if (log_open)
  {
    log_file.close();
    log_open = false;
  }
}

void LOG(const char *text, ...)
{
#ifdef VPDEBUG
  if (!dumping) return;
#endif
	if (!log_open)
    return;
	va_list ap;
	va_start(ap, text);
	vsprintf(out_buf, text, ap);
  log_file << out_buf;
  log_file.flush();
	va_end(ap);
}

class LogManager {
public:
	LogManager() {
		OPEN_LOG();
	}
	~LogManager() {
		CLOSE_LOG();
	}
};

LogManager logManager;

#else // LOGGING
#define OPEN_LOG()
#define CLOSE_LOG()
//#define LOG
#endif // LOGGING
