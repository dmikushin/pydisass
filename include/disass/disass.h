#ifndef DISASS_H
#define DISASS_H

#include <nlohmann/json.hpp>

nlohmann::json disass(const std::string& binary, const std::string& mcpu, uint64_t offset);

#endif // DISASS_H
