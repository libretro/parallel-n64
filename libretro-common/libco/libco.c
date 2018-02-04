/*
  libco
  auto-selection module
  license: public domain
*/

#if defined _MSC_VER
  #include <Windows.h>
  #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP)
    #include "fiber.c"
  #elif defined _M_IX86
    #include "x86.c"
  #elif defined _M_AMD64
    #include "amd64.c"
  #else
    #include "fiber.c"
  #endif
#elif defined __GNUC__
  #if defined __i386__
    #include "x86.c"
  #elif defined __amd64__
    #include "amd64.c"
  #elif defined _ARCH_PPC
    #include "ppc.c"
  #elif defined VITA
    #include "scefiber.c"
  #elif defined(__ARM_EABI__) || defined(__arm__)
    #include "armeabi.c"
  #else
    #include "sjlj.c"
  #endif
#else
  #error "libco: unsupported processor, compiler or operating system"
#endif
