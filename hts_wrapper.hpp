#pragma once

#include <string>
#include <string_view>

void hts_rans4x16_encode(std::string_view in, std::string& out);
void hts_rans4x16_decode(std::string_view in, std::string& out);

void hts_arith_encode(std::string_view in, std::string& out);
void hts_arith_decode(std::string_view in, std::string& out);
