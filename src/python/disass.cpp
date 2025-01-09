#include "disass/disass.h"

#include <iterator>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <vector>

using namespace disass;

namespace py = pybind11;

py::dict pydisass(const std::string& binary, const std::string& mcpu, uint64_t offset, bool detail) {
    py::dict instructionsDict;

    Disassembler md(mcpu);
    md.detail = detail;
    for (const auto& i : md.disasm(binary, offset)) {
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

