#include "file.h"

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

#include <stdio.h>
#include <stdlib.h>

bool file_exists(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (fp) {
        fclose(fp);
        return true;
    } else {
        return false;
    }
}

bool file_path_indexed(char* path, uint32_t path_size, const char* dir, const char* name, const char* ext, uint32_t* index)
{
    // find a free file slot between 0 and 9999
    for (; *index < 10000; (*index)++) {
        sprintf(path, "%s" PATH_SEPARATOR "%s_%04d.%s", dir, name, *index, ext);
        if (!file_exists(path)) {
            return true;
        }
    }

    // all file slots are used up
    return false;
}
