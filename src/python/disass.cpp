#include "disass/disass.h"

#include <pybind11/pybind11.h>
#include <unordered_map>

namespace py = pybind11;

py::dict pydisass(const std::string& binary, const std::string& mcpu, uint64_t offset) {
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

