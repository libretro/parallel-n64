#ifndef RSP_JIT_HPP__
#define RSP_JIT_HPP__

#include <memory>
#include <stdint.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "rsp_op.hpp"
#include "state.hpp"

extern "C"
{
#include <lightning.h>
}

namespace RSP
{
namespace JIT
{
using Func = jit_pointer_t;

class alignas(64) CPU
{
public:
	CPU();

	~CPU();

	CPU(CPU &&) = delete;

	void operator=(CPU &&) = delete;

	void set_dmem(uint32_t *dmem)
	{
		state.dmem = dmem;
	}

	void set_imem(uint32_t *imem)
	{
		state.imem = imem;
	}

	void set_rdram(uint32_t *rdram)
	{
		state.rdram = rdram;
	}

	void invalidate_imem();

	CPUState &get_state()
	{
		return state;
	}

	ReturnMode run();

	void print_registers();

	Func get_jit_block(uint32_t pc);

private:
	CPUState state;
	Func blocks[IMEM_WORDS] = {};

	void invalidate_code();

	uint64_t hash_imem(unsigned pc, unsigned count) const;

	alignas(64) uint32_t cached_imem[IMEM_WORDS] = {};

	std::unordered_map<uint64_t, Func> cached_blocks[IMEM_WORDS];

	Func jit_region(uint64_t hash, unsigned pc_word, unsigned instruction_count);

	int enter(uint32_t pc);

	std::vector<jit_state_t *> cleanup_jit_states;

	void init_jit_thunks();

	struct
	{
		int (*enter_frame)(void *state) = nullptr;

		Func enter_thunk = nullptr;
		Func return_thunk = nullptr;
	} thunks;

	unsigned analyze_static_end(unsigned pc, unsigned end);

	struct InstructionInfo
	{
		uint32_t branch_target;
		bool indirect;
		bool branch;
		bool conditional;
		bool handles_delay_slot;
	};
	void jit_instruction(jit_state_t *_jit, uint32_t pc, uint32_t instr, InstructionInfo &info, const InstructionInfo &last_info,
	                     bool first_instruction, bool next_instruction_is_target);
	void jit_exit(jit_state_t *_jit, uint32_t pc, const InstructionInfo &last_info, ReturnMode mode, bool first_instruction);
	void jit_exit_dynamic(jit_state_t *_jit, uint32_t pc, const InstructionInfo &last_info, bool first_instruction);
	void jit_end_of_block(jit_state_t *_jit, uint32_t pc, const InstructionInfo &last_info);
	static void jit_load_register(jit_state_t *_jit, unsigned jit_register, unsigned mips_register);
	static void jit_load_register_zext(jit_state_t *_jit, unsigned jit_register, unsigned mips_register);
	static void jit_store_register(jit_state_t *_jit, unsigned jit_register, unsigned mips_register);
	void jit_handle_delay_slot(jit_state_t *_jit, const InstructionInfo &last_info, uint32_t base_pc, uint32_t end_pc);
	void jit_handle_impossible_delay_slot(jit_state_t *_jit, const InstructionInfo &info, const InstructionInfo &last_info,
	                                      uint32_t base_pc, uint32_t end_pc);
	void jit_handle_latent_delay_slot(jit_state_t *_jit, const InstructionInfo &last_info);
	void jit_mark_block_entries(uint32_t pc, uint32_t end, bool *block_entries);
	void jit_emit_load_operation(jit_state_t *_jit, uint32_t pc, uint32_t instr,
	                             void (*jit_emitter)(jit_state_t *_jit, unsigned, unsigned, unsigned), const char *asmop,
	                             jit_pointer_t rsp_unaligned_op,
	                             uint32_t endian_flip,
	                             const InstructionInfo &last_info);
	void jit_emit_store_operation(jit_state_t *_jit, uint32_t pc, uint32_t instr,
	                              void (*jit_emitter)(jit_state_t *_jit, unsigned, unsigned, unsigned), const char *asmop,
	                              jit_pointer_t rsp_unaligned_op,
	                              uint32_t endian_flip,
	                              const InstructionInfo &last_info);

	static void jit_save_caller_save_registers(jit_state_t *_jit);
	static void jit_restore_caller_save_registers(jit_state_t *_jit);
	static void jit_save_illegal_cond_branch_taken(jit_state_t *_jit);
	static void jit_restore_illegal_cond_branch_taken(jit_state_t *_jit, unsigned reg);
	static void jit_clear_illegal_cond_branch_taken(jit_state_t *_jit, unsigned tmp_reg);
	static void jit_clear_cond_branch_taken(jit_state_t *_jit);
	static void jit_save_indirect_register(jit_state_t *_jit, unsigned mips_register);
	static void jit_load_indirect_register(jit_state_t *_jit, unsigned jit_reg);
	static void jit_save_illegal_indirect_register(jit_state_t *_jit);
	static void jit_load_illegal_indirect_register(jit_state_t *_jit, unsigned jit_reg);

	std::string mips_disasm;
	struct Link
	{
		jit_node_t *node;
		unsigned local_index;
	};
	std::vector<Link> local_branches;
};
} // namespace JIT
} // namespace RSP

#endif