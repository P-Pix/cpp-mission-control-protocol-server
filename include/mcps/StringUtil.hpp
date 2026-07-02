#pragma once

#include <string>
#include <vector>

namespace mcps {

std::string trim(std::string value);
std::string to_upper(std::string value);
std::vector<std::string> split_whitespace(const std::string& value);
bool starts_with(const std::string& value, const std::string& prefix);

}  // namespace mcps
