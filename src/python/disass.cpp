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

py::dict pydisass(const std::string& binary, const std::string& mcpu, uint64_t offset, bool detail) {
    py::dict instructionsDict;
    std::istringstream stream(disass(binary, mcpu, offset));
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] != ' ') continue;

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

        py::dict instruction;
        instruction["binary"] = binstr;
        instruction["mnemonic"] = mnemonic;
        instruction["op_str"] = op_str;
        instruction["size"] = size;

        if (detail) {
            std::vector<py::dict> operands;
            std::istringstream opStream(op_str);
            std::string operand;
            while (std::getline(opStream, operand, ',')) {
                operand = trim(operand);
                py::dict operandDict;
                operandDict["text"] = operand;

                // Check if the operand is an immediate value
                if (operand.length() > 2 && operand.find("0x") != std::string::npos) {
                    py::dict immDict;
                    unsigned long intValue = std::stoul(operand, nullptr, 16);
                    uint32_t immValue = static_cast<uint32_t>(intValue);
                    immDict["imm"] = immValue;
                    operandDict["value"] = immDict;
                }
                else if (operand.length() > 1 && operand[0] == '#') {
                    operand[0] = ' ';
                    py::dict immDict;
                    unsigned long intValue = std::stoul(operand, nullptr, 10);
                    uint32_t immValue = static_cast<uint32_t>(intValue);
                    immDict["imm"] = immValue;
                    operandDict["value"] = immDict;
                }

                operands.push_back(operandDict);
            }
            instruction["operands"] = operands;
        }

        if (!constant.empty()) {
            instruction["constant"] = constant;
        }

        instructionsDict[std::to_string(address).c_str()] = instruction;
    }

    return instructionsDict;
}

PYBIND11_MODULE(pydisass_native, m)
{
    m.def("disass", &pydisass, "Disassemble the given binary");
}

