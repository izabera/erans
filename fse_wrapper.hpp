#pragma once

#include <string>
#include <string_view>

void fse_encode(std::string_view in, std::string& out);
void fse_decode(std::string_view in, std::string& out);
