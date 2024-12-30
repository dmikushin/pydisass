#ifndef DISASS_H
#define DISASS_H

#include <nlohmann/json.hpp>

std::string disass(const std::string& binary, const std::string& mcpu, uint64_t offset);

nlohmann::json disass_json(const std::string& binary, const std::string& mcpu, uint64_t offset);

#endif // DISASS_H
