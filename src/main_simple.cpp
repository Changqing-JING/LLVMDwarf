// Simple DWARF Generator using LLVM's DIE classes
// - Uses DIEEntry for automatic type reference management
// - Direct human-readable output (no binary serialization)

#include <map>
#include <string>

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Simple string pool for offset tracking
class SimpleStringPool {
  std::string data;
  std::map<std::string, uint32_t> offsets;

public:
  uint32_t add(const std::string &str) {
    auto it = offsets.find(str);
    if (it != offsets.end()) {
      return it->second;
    }
    uint32_t offset = data.size();
    offsets[str] = offset;
    data += str;
    data += '\0';
    return offset;
  }

  const std::string &getData() const {
    return data;
  }
  uint32_t getSize() const {
    return data.size();
  }

  std::string getStringAt(uint32_t offset) const {
    if (offset >= data.size())
      return "";
    return std::string(data.c_str() + offset);
  }
};

// Print a single DIE with indentation
void printDIE(raw_ostream &OS, DIE &die, SimpleStringPool &stringPool, int indent = 0) {
  std::string indentStr(indent, ' ');

  // Print DIE header
  OS << indentStr << "0x" << format("%08x", die.getOffset()) << ": ";
  OS << dwarf::TagString(die.getTag());
  OS << " [" << die.getAbbrevNumber() << "]";

  if (die.hasChildren()) {
    OS << " *\n";
  } else {
    OS << "\n";
  }

  // Print attributes
  for (const auto &V : die.values()) {
    OS << indentStr << "  " << dwarf::AttributeString(V.getAttribute()) << " = ";

    switch (V.getType()) {
    case DIEValue::isInteger: {
      uint64_t val = V.getDIEInteger().getValue();

      // Special handling for string offsets
      if (V.getForm() == dwarf::DW_FORM_strp) {
        std::string str = stringPool.getStringAt(val);
        OS << "\"" << str << "\" (strp offset: 0x" << format("%08x", val) << ")";
      } else if (V.getAttribute() == dwarf::DW_AT_encoding) {
        OS << dwarf::AttributeEncodingString(val);
      } else {
        OS << "0x" << format("%x", val);
      }
      break;
    }
    case DIEValue::isEntry: {
      DIE &refDie = V.getDIEEntry().getEntry();
      OS << "{0x" << format("%08x", refDie.getOffset()) << "}";
      break;
    }
    default:
      OS << "<unknown type>";
      break;
    }

    OS << " [" << dwarf::FormEncodingString(V.getForm()) << "]\n";
  }

  // Print children recursively
  for (auto &child : die.children()) {
    printDIE(OS, child, stringPool, indent + 2);
  }

  if (die.hasChildren()) {
    OS << indentStr << "NULL\n";
  }
}

int main() {
  // Create allocator for DIE objects
  BumpPtrAllocator allocator;
  DIEAbbrevSet abbrevSet(allocator);
  SimpleStringPool stringPool;

  // Create compile unit DIE
  DIE *cu = DIE::get(allocator, dwarf::DW_TAG_compile_unit);
  cu->addValue(allocator, dwarf::DW_AT_producer, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("warpo")));
  cu->addValue(allocator, dwarf::DW_AT_language, dwarf::DW_FORM_data2, DIEInteger(dwarf::DW_LANG_C_plus_plus));

  // Create int base type
  DIE *intType = DIE::get(allocator, dwarf::DW_TAG_base_type);
  intType->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("int")));
  intType->addValue(allocator, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1, DIEInteger(dwarf::DW_ATE_signed));
  intType->addValue(allocator, dwarf::DW_AT_byte_size, dwarf::DW_FORM_data1, DIEInteger(4));
  cu->addChild(intType);

  // Create char base type
  DIE *charType = DIE::get(allocator, dwarf::DW_TAG_base_type);
  charType->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("char")));
  charType->addValue(allocator, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1, DIEInteger(dwarf::DW_ATE_signed_char));
  charType->addValue(allocator, dwarf::DW_AT_byte_size, dwarf::DW_FORM_data1, DIEInteger(1));
  cu->addChild(charType);

  // Create char* pointer type
  DIE *charPtrType = DIE::get(allocator, dwarf::DW_TAG_pointer_type);
  charPtrType->addValue(allocator, dwarf::DW_AT_byte_size, dwarf::DW_FORM_data1, DIEInteger(8));
  charPtrType->addValue(allocator, dwarf::DW_AT_type, dwarf::DW_FORM_ref4,
                        DIEEntry(*charType)); // Automatic reference!
  cu->addChild(charPtrType);

  // Create MyClass structure
  DIE *classType = DIE::get(allocator, dwarf::DW_TAG_structure_type);
  classType->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("MyClass")));
  classType->addValue(allocator, dwarf::DW_AT_byte_size, dwarf::DW_FORM_data1, DIEInteger(24));
  cu->addChild(classType);

  // Add member 'x' (int)
  DIE *memberX = DIE::get(allocator, dwarf::DW_TAG_member);
  memberX->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("x")));
  memberX->addValue(allocator, dwarf::DW_AT_type, dwarf::DW_FORM_ref4,
                    DIEEntry(*intType)); // Automatic reference!
  memberX->addValue(allocator, dwarf::DW_AT_data_member_location, dwarf::DW_FORM_data1, DIEInteger(0));
  classType->addChild(memberX);

  // Add member 'y' (int)
  DIE *memberY = DIE::get(allocator, dwarf::DW_TAG_member);
  memberY->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("y")));
  memberY->addValue(allocator, dwarf::DW_AT_type, dwarf::DW_FORM_ref4,
                    DIEEntry(*intType)); // Same type reference!
  memberY->addValue(allocator, dwarf::DW_AT_data_member_location, dwarf::DW_FORM_data1, DIEInteger(4));
  classType->addChild(memberY);

  // Add member 'name' (char*)
  DIE *memberName = DIE::get(allocator, dwarf::DW_TAG_member);
  memberName->addValue(allocator, dwarf::DW_AT_name, dwarf::DW_FORM_strp, DIEInteger(stringPool.add("name")));
  memberName->addValue(allocator, dwarf::DW_AT_type, dwarf::DW_FORM_ref4,
                       DIEEntry(*charPtrType)); // Automatic reference!
  memberName->addValue(allocator, dwarf::DW_AT_data_member_location, dwarf::DW_FORM_data1, DIEInteger(8));
  classType->addChild(memberName);

  // Compute offsets and assign abbreviation numbers
  dwarf::FormParams formParams = {4, 4, dwarf::DWARF32};
  cu->computeOffsetsAndAbbrevs(formParams, abbrevSet, 11);

  outs() << "✓ DIE tree built with automatic reference management\n";
  outs() << "✓ computeOffsetsAndAbbrevs() resolved all DIEEntry references\n";
  outs() << "✓ Producer: warpo\n";
  outs() << "✓ Class: MyClass with members (x:int, y:int, name:char*)\n\n";

  // Write to file
  std::error_code EC;
  raw_fd_ostream dumpFile("debug.txt", EC, sys::fs::OF_None);
  if (EC) {
    errs() << "Error opening debug.txt: " << EC.message() << "\n";
    return 1;
  }

  dumpFile << "=== DWARF Debug Information ===\n";
  dumpFile << "Producer: warpo\n";
  dumpFile << "Language: C++\n\n";

  dumpFile << ".debug_info contents:\n";
  printDIE(dumpFile, *cu, stringPool);

  dumpFile << "\n.debug_str contents:\n";
  uint32_t offset = 0;
  const std::string &strData = stringPool.getData();
  while (offset < strData.size()) {
    std::string str = stringPool.getStringAt(offset);
    dumpFile << "0x" << format("%08x", offset) << ": \"" << str << "\"\n";
    offset += str.size() + 1;
  }

  dumpFile.close();
  outs() << "✓ Human-readable DWARF dump written to debug.txt\n";

  return 0;
}
