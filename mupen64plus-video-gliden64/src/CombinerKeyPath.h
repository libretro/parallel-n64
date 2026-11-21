/* Where a GL plugin keeps its combiner keys. C89. */
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

/* The journal every game shares at @path (the shaders folder, which
 * @folder is, and <plugin>.keys), and @game, @header's CRCs in the hex
 * digits that name its lines: COMBINER_JOURNAL_GAME and a NUL */
static INLINE void combiner_key_shared(char *path, char *folder, size_t size,
      const char *plugin, char *game, const unsigned char *header)
{
   char name[64];
   const char *system = retro_get_system_directory();

   fill_pathname_join(folder, system ? system : "", "ParallelN64", size);
   fill_pathname_join(folder, folder, "shaders", size);
   path_mkdir(folder);
   snprintf(name, sizeof(name), "%s.keys", plugin);
   fill_pathname_join(path, folder, name, size);
   snprintf(game, 17, "%08x%08x",
         *(const uint32_t*)(header + 0x10), *(const uint32_t*)(header + 0x14));
}

#endif
