#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FILE_MAX_PATH 256

bool file_exists(const char* path);
bool file_path_indexed(char* path, uint32_t path_size, const char* dir, const char* name, const char* ext, uint32_t* index);
