#pragma once

#include <string>
#include <string_view>

void ryg_rans64_encode(std::string_view in, std::string& out);
void ryg_rans64_decode(std::string_view in, std::string& out);
