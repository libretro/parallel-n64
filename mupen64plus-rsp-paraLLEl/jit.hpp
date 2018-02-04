#ifndef JIT_HPP__
#define JIT_HPP__

#include <memory>
#include <unordered_map>
#include <string>

namespace JIT
{
   using Func = void (*)(void *, void *);
   class Block
   {
      public:
         Block(const std::unordered_map<std::string, uint64_t> &symbol_table);
         ~Block();
         bool compile(uint64_t hash, const std::string &source);
         Func get_func() const { return block; }

      private:
         struct Impl;
         std::unique_ptr<Impl> impl;
         const std::unordered_map<std::string, uint64_t> &symbol_table;

         Func block = nullptr;
         size_t block_size = 0;
   };
}

#endif
