#ifndef DISASS_H
#define DISASS_H

#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#ifdef disass_EXPORTS
#define DISASS_API __attribute__((visibility("default")))
#else
#define DISASS_API
#endif // disass_EXPORTS

namespace disass {

DISASS_API std::string disass(const std::string& binary, const std::string& mcpu, uint64_t offset);

DISASS_API nlohmann::json disass_json(const std::string& binary, const std::string& mcpu, uint64_t offset, bool detail = false);

struct Value {
    int imm; // 32-bit integer
};

struct Operand {
    std::string text;
    std::optional<Value> value;
};

struct Instruction {
    int address;
    std::string binary;
    std::string mnemonic;
    std::string op_str;
    int size;
    std::vector<Operand> operands;
    std::optional<std::string> constant;

    friend std::ostream& operator<<(std::ostream& os, const Instruction& instr);
};

class DISASS_API AssemblyParser
{
    std::istringstream stream;
    uint64_t offset;
    bool& detail;

public :

    AssemblyParser(
        const std::string& binary, uint64_t offset, bool& detail,
        const std::string& cpu_model = "arm926ej-s", const std::string& triple = "arm-none-eabi");
    
    bool next_instruction(Instruction& instruction);
};

class DISASS_API InstructionIterator {
    AssemblyParser* parser;
    Instruction currentInstruction;
    bool hasNext;

public:
    using iterator_category = std::input_iterator_tag;
    using value_type = Instruction;
    using difference_type = std::ptrdiff_t;
    using pointer = Instruction*;
    using reference = Instruction&;

    InstructionIterator(AssemblyParser* parser);

    InstructionIterator& operator++() {
        hasNext = parser->next_instruction(currentInstruction);
        return *this;
    }

    bool operator!=(const InstructionIterator& other) const {
        return hasNext != other.hasNext;
    }

    const Instruction& operator*() const {
        return currentInstruction;
    }
};

class DISASS_API IterableAssemblyParser : public AssemblyParser {
public:
    using AssemblyParser::AssemblyParser;

    InstructionIterator begin() {
        return InstructionIterator(this);
    }

    InstructionIterator end() {
        return InstructionIterator(nullptr);
    }
};

class DISASS_API Disassembler {
    const std::string cpu_model;
    const std::string triple;

public:
    bool detail = false;

    Disassembler(const std::string& cpu_model_ = "arm926ej-s", const std::string& triple_ = "arm-none-eabi") :
        cpu_model(cpu_model_), triple(triple_)
    { }

    IterableAssemblyParser disasm(const std::string& binary, uint64_t offset)
    {
        return IterableAssemblyParser(binary, offset, detail, cpu_model, triple);
    }
};

} // namespace disass

#endif // DISASS_H

