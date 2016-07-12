#include "rsp_dump.h"
#include <unordered_set>

static std::unordered_set<uint64_t> seen_ucodes;

static inline uint64_t hash64(const void *data_, size_t size)
{
	// FNV-1.
	const uint8_t *data = static_cast<const uint8_t *>(data_);
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < size; i++)
		h = (h * 0x100000001b3ull) ^ data[i];
	return h;
}

void rsp_dump_imem(const void *imem, size_t size)
{
   uint64_t hash = hash64(imem, size);
   auto itr = seen_ucodes.find(hash);
   if (itr == end(seen_ucodes))
   {
      char path[128];
      sprintf(path, "/tmp/%05zu.imem", seen_ucodes.size());
      fprintf(stderr, "Dumping RSP IMEM to %s.\n", path);
#if 0
      FILE *file = fopen(path, "wb");
      if (file)
      {
         fwrite(imem, size, 1, file);
         fclose(file);
      }
#endif
      seen_ucodes.insert(hash);
   }
}
