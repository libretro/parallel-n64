#ifndef _GLITCH_LOG_H
#define _GLITCH_LOG_H

void display_warning(const char *text, ...);

#ifdef _WIN32
void display_error(void);
#endif

#ifdef LOGGING
void OPEN_LOG(void);
void CLOSE_LOG(void);
void LOG(const char *text, ...);
void LOGINFO(const char *text, ...);
#else
#define OPEN_LOG()
#define CLOSE_LOG()
#define LOG(...) ((void)0)
#define LOGINFO(...) ((void)0)
#endif

#endif
