#include "disass/disass.h"

#include <pybind11/pybind11.h>
#include "pybind11_json/pybind11_json.hpp"

namespace py = pybind11;

py::dict pydisass(const std::string& binary, const std::string& mcpu, uint64_t offset)
{
    nlohmann::json result = disass(binary, mcpu, offset);
    return py::dict(result);
}

PYBIND11_MODULE(pydisass_native, m)
{
    m.def("disass", &pydisass, "Disassemble the given binary");
}

