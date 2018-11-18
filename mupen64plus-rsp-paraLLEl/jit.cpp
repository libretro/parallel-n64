#include "jit.hpp"



#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Tool.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <stdio.h>
using namespace clang;
using namespace std;


namespace JIT
{

struct Block::Impl
{
   Impl(const unordered_map<string, uint64_t> &symbol_table)
      : symbol_table(symbol_table)
   {}

   Func block = nullptr;
   size_t block_size = 0;
   bool compile(const std::string &source);
   const unordered_map<string, uint64_t> &symbol_table;
};

Block::Block(const unordered_map<string, uint64_t> &symbol_table)
   : symbol_table(symbol_table)
{
   impl = std::unique_ptr<Impl>(new Impl(symbol_table));
}

Block::~Block()
{
}

struct RSPResolver : public llvm::JITSymbolResolver
{
public:
   RSPResolver(const unordered_map<string, uint64_t> &symbol_table)
      : symbol_table(symbol_table)
   {}

   llvm::JITSymbol findSymbol(const std::string &name) override
   {
      auto itr = symbol_table.find(name);
      if (itr != end(symbol_table))
         return llvm::JITSymbol(itr->second, llvm::JITSymbolFlags::None);
      else
         return llvm::JITSymbol(nullptr);
   }

   llvm::JITSymbol findSymbolInLogicalDylib(const std::string &name) override
   {
      return llvm::JITSymbol(nullptr);
   }

   const unordered_map<string, uint64_t> &symbol_table;
};

struct LLVMHolder
{
   LLVMHolder()
   {
      llvm::InitializeNativeTarget();
      llvm::InitializeNativeTargetAsmPrinter();
      llvm::InitializeNativeTargetAsmParser();
   }

   ~LLVMHolder()
   {
      llvm::llvm_shutdown();
   }
};

struct LLVMEngine
{
   LLVMEngine()
   {
      SmallVector<const char *, 4> args;
      args.push_back("__block.c");
      args.push_back("-std=c99");
      args.push_back("-O2");

      static std::string string_buffer;
      static llvm::raw_string_ostream ss(string_buffer);

      IntrusiveRefCntPtr<DiagnosticOptions> diag_opts = new DiagnosticOptions();
      TextDiagnosticPrinter *diag_client = new TextDiagnosticPrinter(ss, &*diag_opts);
      IntrusiveRefCntPtr<DiagnosticIDs> diag_id(new DiagnosticIDs());
      DiagnosticsEngine diags(diag_id, &*diag_opts, diag_client);

      CI = llvm::make_unique<CompilerInvocation>();
      CompilerInvocation::CreateFromArgs(*CI, args.data(), args.data() + args.size(), diags);
      invocation = CI.get();

      clang = llvm::make_unique<CompilerInstance>();
      clang->setInvocation(std::move(CI));
      clang->createDiagnostics();
 
      act = llvm::make_unique<EmitLLVMOnlyAction>();
   }

   Func compile(const std::unordered_map<std::string, uint64_t> &symbol_table)
   {
      if (!clang->ExecuteAction(*act))
      {
         fprintf(stderr, "ExecuteAction failed.");
         return nullptr;
      }

      auto module = act->takeModule();
      auto *tmp_module = module.get();

      if (!EE)
      {
         auto resolver = llvm::make_unique<RSPResolver>(symbol_table);
         auto memory_manager = llvm::make_unique<llvm::SectionMemoryManager>();

        EE = std::unique_ptr<llvm::ExecutionEngine>(llvm::EngineBuilder(std::move(module))
         .setMCJITMemoryManager(std::move(memory_manager))
         .setSymbolResolver(std::move(resolver))
         .create());
                
         EE->DisableLazyCompilation(true);
      }
      else
         EE->addModule(std::move(module));

      if (!EE)
      {
         llvm::errs() << "Failed to make execution engine.\n";
         return nullptr;
      }

      EE->finalizeObject();
      auto entry_point = EE->getFunctionAddress("block_entry");
      auto block = reinterpret_cast<Func>(entry_point);
      EE->removeModule(tmp_module);
      return block;
   }

   std::unique_ptr<LLVMHolder> llvm = llvm::make_unique<LLVMHolder>();

   std::string string_buffer;
   llvm::raw_string_ostream ss{string_buffer};

   std::unique_ptr<CompilerInvocation> CI;
   std::unique_ptr<CompilerInstance> clang;
   std::unique_ptr<CodeGenAction> act;
   std::unique_ptr<llvm::ExecutionEngine> EE;
   CompilerInvocation *invocation = nullptr;
};

bool Block::compile(uint64_t, const std::string &source)
{
   impl = std::unique_ptr<Impl>(new Impl(symbol_table));
   bool ret = impl->compile(source);
   if (ret)
   {
      block = impl->block;
      block_size = impl->block_size;
   }
   return ret;
}

bool Block::Impl::compile(const std::string &source)
{
   static LLVMEngine llvm;

   StringRef code_data(source);
   auto buffer = llvm::MemoryBuffer::getMemBufferCopy(code_data);
    llvm.invocation->getPreprocessorOpts().clearRemappedFiles();
    llvm.invocation->getPreprocessorOpts().addRemappedFile("__block.c", buffer.release());

   block = llvm.compile(symbol_table);
   return block != nullptr;
}

}
