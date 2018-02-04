#ifndef RDP_DUMP_HPP
#define RDP_DUMP_HPP

#include <stdint.h>
#include <vector>

namespace RDP
{
enum DumpCommand
{
	RDP_DUMP_CMD_INVALID = 0,
	RDP_DUMP_CMD_UPDATE_DRAM = 1,
	RDP_DUMP_CMD_BEGIN_COMMAND_LIST = 2,
	RDP_DUMP_CMD_END_COMMAND_LIST = 3,
	RDP_DUMP_CMD_RDP_COMMAND = 4,
	RDP_DUMP_CMD_EOF = 5,
};

struct DRAMUpdate
{
	uint32_t offset;
	std::vector<uint8_t> payload;
};

struct Command
{
	uint32_t command;
	std::vector<uint32_t> arguments;
};

struct CommandList
{
	std::vector<DRAMUpdate> dram_updates;
	std::vector<Command> commands;
};

struct Dump
{
	std::vector<CommandList> lists;
	uint32_t dram_size;
};

bool load_dump(const char *path, Dump *dump);
}

#endif
