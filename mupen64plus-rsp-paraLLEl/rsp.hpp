#ifndef RSP_HPP__
#define RSP_HPP__

#include <stdint.h>
#include <string.h>
#include <unordered_map>
#include <memory>
#include <string>

#include "state.hpp"
#include "jit.hpp"
#include "debug_jit.hpp"
#include "rsp_op.hpp"

#include <setjmp.h>

namespace RSP
{
#ifdef DEBUG_JIT
   using Block = JIT::DebugBlock;
#else
   using Block = JIT::Block;
#endif
   using Func = JIT::Func;

   enum ReturnMode
   {
      MODE_ENTER = 0,
      MODE_CONTINUE = 1,
      MODE_BREAK = 2,
      MODE_DMA_READ = 3,
      MODE_CHECK_FLAGS = 4
   };

   class alignas(64) CPU
   {
      public:
         CPU();
         ~CPU();

         CPU(CPU&&) = delete;
         void operator=(CPU&&) = delete;

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

         void enter(uint32_t pc);
         void call(uint32_t target, uint32_t ret);
         int ret(uint32_t pc);
         void exit(ReturnMode mode);

      private:
         CPUState state;
         Func blocks[IMEM_WORDS] = {};
         std::unordered_map<uint64_t, std::unique_ptr<Block>> cached_blocks[IMEM_WORDS];

         void invalidate_code();
         uint64_t hash_imem(unsigned pc, unsigned count) const;
         Func jit_region(uint64_t hash, unsigned pc, unsigned count);

         std::string full_code;
         std::string body;

         std::unordered_map<std::string, uint64_t> symbol_table;

         void init_symbol_table();
         void print_registers();

         uint32_t cached_imem[IMEM_WORDS] alignas(64) = {};

         // Platform specific.
#ifdef _WIN32
         jmp_buf env;
#else
         sigjmp_buf env;
#endif

#define CALL_STACK_SIZE 32
         uint32_t call_stack[CALL_STACK_SIZE] = {};
         unsigned call_stack_ptr = 0;

         unsigned analyze_static_end(unsigned pc, unsigned end);
   };
}

#endif
