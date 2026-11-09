/* Where a GL plugin keeps a game's combiner keys. C89. */
#ifndef COMBINER_KEY_PATH_H
#define COMBINER_KEY_PATH_H

#include <stdio.h>
#include <stdint.h>
#include <file/file_path.h>
#include <retro_inline.h>
#include <retro_miscellaneous.h>

#ifdef __cplusplus
extern "C" {
#endif
const char *retro_get_system_directory(void);
#ifdef __cplusplus
}
#endif

/* @header is the game's ROM header: its CRCs name the file. */
static INLINE void combiner_key_path(char *path, size_t size, const char *plugin,
      const unsigned char *header)
{
   char dir[PATH_MAX_LENGTH];
   char name[64];
   const char *system = retro_get_system_directory();

   fill_pathname_join(dir, system ? system : "", "ParallelN64", sizeof(dir));
   fill_pathname_join(dir, dir, "shaders", sizeof(dir));
   path_mkdir(dir);
   snprintf(name, sizeof(name), "%s_%08x%08x.keys", plugin,
         *(const uint32_t*)(header + 0x10), *(const uint32_t*)(header + 0x14));
   fill_pathname_join(path, dir, name, size);
}

#endif
