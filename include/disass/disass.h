#ifndef DISASS_H
#define DISASS_H

#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

std::string disass(const std::string& binary, const std::string& mcpu, uint64_t offset);

nlohmann::json disass_json(const std::string& binary, const std::string& mcpu, uint64_t offset);

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

#endif // DISASS_H

