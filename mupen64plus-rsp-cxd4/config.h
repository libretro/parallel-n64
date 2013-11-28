#define _CRT_SECURE_NO_WARNINGS
/*
 * This is only here for people using modern Microsoft compilers.
 * Usually the default warning level complains over "deprecated" CRT methods.
 * It's basically Microsoft's way of saying they're better than everyone.
 */

#define MINIMUM_MESSAGE_PRIORITY    4
#define EXTERN_COMMAND_LIST_GBI
//#define EXTERN_COMMAND_LIST_ABI
//#define SEMAPHORE_LOCK_CORRECTIONS
//#define WAIT_FOR_CPU_HOST
//#define EMULATE_STATIC_PC

#if (0)
#define SP_EXECUTE_LOG
#define VU_EMULATE_SCALAR_ACCUMULATOR_READ
#endif

const char DLL_name[100] = "Iconoclast's SP Interpreter";