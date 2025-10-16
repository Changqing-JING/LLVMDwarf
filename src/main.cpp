#include <fstream>
#include <memory>
#include <sstream>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;
using namespace llvm::object;

int main() {
  // Create LLVM context and module
  LLVMContext context;
  auto module = std::make_unique<Module>("test_module", context);

  // Create DIBuilder
  DIBuilder builder(*module);

  // Create a compile unit with producer "warpo"
  // Use a placeholder file (required by LLVM, but we won't use line info)
  DIFile *file = builder.createFile("<unknown>", "");

  DICompileUnit *CU = builder.createCompileUnit(dwarf::DW_LANG_C_plus_plus, // Language
                                                file,                       // File
                                                "warpo",                    // Producer (the name we want)
                                                false,                      // isOptimized
                                                "",                         // Flags
                                                0                           // Runtime Version
  );

  // Create a class/struct type to describe layout
  // Example: class MyClass with member variables (no functions)

  // Create basic types first
  DIBasicType *intType = builder.createBasicType("int", 32, dwarf::DW_ATE_signed);
  DIBasicType *charType = builder.createBasicType("char", 8, dwarf::DW_ATE_signed_char);

  // Create a pointer type
  DIDerivedType *charPtrType = builder.createPointerType(charType, 64);

  // Create a composite type (class/struct)
  DICompositeType *classType = builder.createClassType(CU,               // Scope (compile unit)
                                                       "MyClass",        // Name
                                                       nullptr,          // File (no source file)
                                                       0,                // Line number (no line info)
                                                       128,              // Size in bits (16 bytes)
                                                       64,               // Alignment in bits
                                                       0,                // Offset
                                                       DINode::FlagZero, // Flags
                                                       nullptr,          // Derived from
                                                       nullptr           // Elements (will add members)
  );

  // Create member variables for the class
  SmallVector<Metadata *, 4> members;

  // Member 1: int x at offset 0
  DIDerivedType *member1 = builder.createMemberType(classType,        // Scope
                                                    "x",              // Name
                                                    nullptr,          // File (no source)
                                                    0,                // Line (no line info)
                                                    32,               // Size in bits
                                                    32,               // Alignment
                                                    0,                // Offset in bits
                                                    DINode::FlagZero, // Flags
                                                    intType           // Type
  );
  members.push_back(member1);

  // Member 2: int y at offset 32 bits (4 bytes)
  DIDerivedType *member2 = builder.createMemberType(classType,        // Scope
                                                    "y",              // Name
                                                    nullptr,          // File (no source)
                                                    0,                // Line (no line info)
                                                    32,               // Size in bits
                                                    32,               // Alignment
                                                    32,               // Offset in bits
                                                    DINode::FlagZero, // Flags
                                                    intType           // Type
  );
  members.push_back(member2);

  // Member 3: char* name at offset 64 bits (8 bytes)
  DIDerivedType *member3 = builder.createMemberType(classType,        // Scope
                                                    "name",           // Name
                                                    nullptr,          // File (no source)
                                                    0,                // Line (no line info)
                                                    64,               // Size in bits (pointer)
                                                    64,               // Alignment
                                                    64,               // Offset in bits
                                                    DINode::FlagZero, // Flags
                                                    charPtrType       // Type
  );
  members.push_back(member3);

  // Replace the class elements with the members array
  builder.replaceArrays(classType, builder.getOrCreateArray(members));

  // Add the class type to the compile unit's retained types
  // This ensures the type appears in DWARF even without a variable
  CU->replaceRetainedTypes(MDTuple::get(context, {classType}));

  // Finalize the debug info
  builder.finalize();

  // Verify the module
  std::string errorMsg;
  raw_string_ostream errorStream(errorMsg);
  if (verifyModule(*module, &errorStream)) {
    errs() << "Module verification failed: " << errorMsg << "\n";
    return 1;
  }

  // Initialize targets for code generation
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();

  // Set up target machine
  std::string targetTriple = sys::getProcessTriple();
  module->setTargetTriple(targetTriple);

  std::string error;
  const Target *target = TargetRegistry::lookupTarget(targetTriple, error);
  if (!target) {
    errs() << "Error: " << error << "\n";
    return 1;
  }

  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  TargetMachine *targetMachine = target->createTargetMachine(targetTriple, "generic", "", opt, RM);
  module->setDataLayout(targetMachine->createDataLayout());

  // Emit to in-memory buffer instead of file
  SmallVector<char, 0> objBuffer;
  raw_svector_ostream objStream(objBuffer);

  legacy::PassManager pass;
  if (targetMachine->addPassesToEmitFile(pass, objStream, nullptr, CodeGenFileType::ObjectFile)) {
    errs() << "TargetMachine can't emit object file\n";
    return 1;
  }

  pass.run(*module);

  outs() << "✓ Compile Unit created with DIBuilder\n";
  outs() << "✓ Producer name: warpo\n";
  outs() << "✓ Class 'MyClass' defined with 3 members\n";
  outs() << "✓ Generated object in memory (" << objBuffer.size() << " bytes)\n\n";

  // Parse the object file from memory
  StringRef objData(objBuffer.data(), objBuffer.size());
  Expected<std::unique_ptr<ObjectFile>> objOrErr = ObjectFile::createObjectFile(MemoryBufferRef(objData, "memory"));

  if (!objOrErr) {
    errs() << "Failed to parse object file\n";
    return 1;
  }

  std::unique_ptr<ObjectFile> &obj = objOrErr.get();

  // Get DWARF section sizes (keep in memory, don't write to disk)
  size_t debugInfoSize = 0, debugAbbrevSize = 0, debugStrSize = 0;
  for (const SectionRef &Section : obj->sections()) {
    Expected<StringRef> nameOrErr = Section.getName();
    if (!nameOrErr)
      continue;

    StringRef name = *nameOrErr;
    Expected<StringRef> contentsOrErr = Section.getContents();
    if (!contentsOrErr)
      continue;

    StringRef contents = *contentsOrErr;

    if (name == ".debug_info") {
      debugInfoSize = contents.size();
      outs() << "✓ DWARF .debug_info: " << debugInfoSize << " bytes (in memory)\n";
    } else if (name == ".debug_abbrev") {
      debugAbbrevSize = contents.size();
      outs() << "✓ DWARF .debug_abbrev: " << debugAbbrevSize << " bytes (in memory)\n";
    } else if (name == ".debug_str") {
      debugStrSize = contents.size();
      outs() << "✓ DWARF .debug_str: " << debugStrSize << " bytes (in memory)\n";
    }
  }

  // Create DWARF context and dump to human-readable file
  std::unique_ptr<DWARFContext> dwCtx = DWARFContext::create(*obj);

  std::error_code EC;
  // First dump to a temporary string
  std::string tempOutput;
  raw_string_ostream tempStream(tempOutput);

  DIDumpOptions dumpOptions;
  dumpOptions.ShowChildren = true;
  dumpOptions.ShowParents = false;
  dumpOptions.ShowForm = true;
  dumpOptions.Verbose = true;
  dumpOptions.SummarizeTypes = false;
  dwCtx->dump(tempStream, dumpOptions);
  tempStream.flush();

  // Now filter out the debug_line section
  raw_fd_ostream dumpFile("debug.txt", EC, sys::fs::OF_None);
  if (!EC) {
    std::istringstream iss(tempOutput);
    std::string line;
    bool inDebugLine = false;

    while (std::getline(iss, line)) {
      // Check if we're entering debug_line section
      if (line.find(".debug_line contents:") != std::string::npos) {
        inDebugLine = true;
        continue;
      }
      // Check if we're entering a new section (exit debug_line)
      if (inDebugLine && line.find(".debug_") != std::string::npos && line.find("contents:") != std::string::npos) {
        inDebugLine = false;
      }
      // Output line if not in debug_line section
      if (!inDebugLine) {
        dumpFile << line << "\n";
      }
    }

    dumpFile.close();
    outs() << "\n✓ Human-readable DWARF dump written to debug.txt (without debug_line)\n";
  } else {
    errs() << "Failed to write debug.txt: " << EC.message() << "\n";
    return 1;
  }

  outs() << "\n✓ Complete! Binary DWARF kept in memory, human-readable dump in debug.txt\n";

  return 0;
}