#include "disass/disass.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <vector>

namespace py = pybind11;

inline std::string trim(std::string& str)
{
    str.erase(str.find_last_not_of(' ')+1);         //suffixing spaces
    str.erase(0, str.find_first_not_of(' '));       //prefixing spaces
    return str;
}

class AssemblyParser
{
    std::istringstream stream;
    uint64_t offset;
    bool detail;

public :

    AssemblyParser(
        const std::string& binary, uint64_t offset, bool detail = false,
        const std::string& cpu_model = "arm926ej-s", const std::string& triple = "arm-none-eabi") :
        offset(offset), detail(detail)
    {
        // At this time, we disassemble the entire binary at once,
        // and get a single string for the whole binary. Later we
        // may change this into line-by-line retrieval, but currently
        // there is no need for that. However, we design the APIs to
        // be capable of this already now.
        stream = std::istringstream(disass(binary, cpu_model, offset));
    }
    
    bool next_instruction(Instruction& instruction)
    {
        bool exists = false;
        std::string line;
        while (!exists && std::getline(stream, line)) {
            if (line.empty() || line[0] != ' ') continue;

            exists = true;
            std::istringstream lineStream(line);
            std::string addressStr, binstr, mnemonic, op_str;
            lineStream >> addressStr >> binstr >> mnemonic;
            std::getline(lineStream, op_str);
            op_str = op_str.empty() ? "" : op_str.substr(1); // Remove leading tab

            std::string constant;
            size_t atPos = op_str.find_last_of('@');
            if (atPos != std::string::npos) {
                constant = op_str.substr(atPos + 1);
                op_str = op_str.substr(0, atPos - 1); // Remove "@ constant"
            }

            int address = std::stoi(addressStr, nullptr, 16) + offset;
            int size = binstr.length() / 2;

            instruction.address = address;
            instruction.binary = binstr;
            instruction.mnemonic = mnemonic;
            instruction.op_str = op_str;
            instruction.size = size;

            if (detail) {
                std::vector<Operand> operands;
                std::istringstream opStream(op_str);
                std::string op;

                while (std::getline(opStream, op, ',')) {
                    op = trim(op);
                    Operand operand;
                    operand.text = op;
                    
                    // Check if the operand is an immediate value
                    int base = 0;
                    if (op.length() > 2 && op[0] == '0' && op[1] == 'x')
                        base = 16;
                    else if (op.length() > 1 && op[0] == '#') {
                        op[0] = ' ';
                        base = 10;
                    }
                    
                    if (base != 0) {
                        py::dict immDict;
                        uint64_t intValue = std::stoull(op, nullptr, base);
                        uint32_t immValue = static_cast<uint32_t>(intValue);
                        Value value;
                        value.imm = immValue;
                        operand.value = value;
                    }

                    operands.push_back(operand);
                }

                instruction.operands = operands;
            }

            if (!constant.empty()) {
                instruction.constant = constant;
            }
        }
        
        return exists;
    }
};

py::dict pydisass(const std::string& binary, const std::string& mcpu, uint64_t offset, bool detail) {
    py::dict instructionsDict;
    std::istringstream stream(disass(binary, mcpu, offset));
    std::string line;

    AssemblyParser parser(binary, offset, detail, mcpu);
    Instruction i;
    while (parser.next_instruction(i)) {
        py::dict instruction;
        instruction["binary"] = i.binary;
        instruction["mnemonic"] = i.mnemonic;
        instruction["op_str"] = i.op_str;
        instruction["size"] = i.size;

        if (detail) {
            std::vector<py::dict> operands;
            for (const auto& op : i.operands)
            {
                py::dict operandDict;
                operandDict["text"] = op.text;
                
                if (op.value.has_value()) {
                    py::dict immDict;
                    immDict["imm"] = op.value->imm;
                    operandDict["value"] = immDict;
                }

                operands.push_back(operandDict);
            }
            instruction["operands"] = operands;
        }

        if (i.constant.has_value()) {
            instruction["constant"] = i.constant;
        }

        instructionsDict[std::to_string(i.address).c_str()] = instruction;
    }

    return instructionsDict;
}

PYBIND11_MODULE(pydisass_native, m)
{
    m.def("disass", &pydisass, "Disassemble the given binary");
}

