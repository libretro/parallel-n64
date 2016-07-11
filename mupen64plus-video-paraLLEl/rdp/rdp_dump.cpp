#include "rdp_dump.hpp"
#include <memory>
#include <stdio.h>
#include <string.h>

using namespace std;

struct FILEDeleter
{
	void operator()(FILE *fp)
	{
		if (fp)
			fclose(fp);
	}
};

namespace RDP
{
static bool read_u32(uint32_t *v, FILE *file)
{
	return fread(v, sizeof(uint32_t), 1, file) == 1;
}

bool load_dump(const char *path, Dump *dump)
{
	dump->lists.clear();

	unique_ptr<FILE, FILEDeleter> file{ fopen(path, "rb") };
	if (!file)
		return false;

	char header[8];
	if (fread(header, sizeof(header), 1, file.get()) != 1)
		return false;

	if (memcmp(header, "RDPDUMP1", sizeof(header)))
		return false;

	uint32_t dram_size;
	if (!read_u32(&dram_size, file.get()))
		return false;

	dump->dram_size = dram_size;

	std::vector<DRAMUpdate> pending_dram;
	CommandList *list = nullptr;

	bool eof = false;
	while (!eof)
	{
		uint32_t cmd;
		if (!read_u32(&cmd, file.get()))
			return false;

		switch (static_cast<DumpCommand>(cmd))
		{
		case RDP_DUMP_CMD_EOF:
			eof = true;
			break;

		case RDP_DUMP_CMD_UPDATE_DRAM:
		{
			DRAMUpdate update;
			if (!read_u32(&update.offset, file.get()))
				return false;

			uint32_t block_size;
			if (!read_u32(&block_size, file.get()))
				return false;

			update.payload.resize(block_size);
			if (fread(update.payload.data(), 1, block_size, file.get()) != block_size)
				return false;

			pending_dram.push_back(move(update));
			break;
		}

		case RDP_DUMP_CMD_BEGIN_COMMAND_LIST:
			if (list)
				return false;

			dump->lists.emplace_back();
			list = &dump->lists.back();
			swap(list->dram_updates, pending_dram);
			break;

		case RDP_DUMP_CMD_END_COMMAND_LIST:
			if (!list)
				return false;
			list = nullptr;
			break;

		case RDP_DUMP_CMD_RDP_COMMAND:
		{
			uint32_t command, words;
			if (!read_u32(&command, file.get()))
				return false;
			if (!read_u32(&words, file.get()))
				return false;

			Command cmd;
			cmd.command = command;
			cmd.arguments.resize(words);
			if (fread(cmd.arguments.data(), sizeof(uint32_t), words, file.get()) != words)
				return false;
			list->commands.push_back(move(cmd));
			break;
		}

		default:
			return false;
		}
	}

	return true;
}
}
