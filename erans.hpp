#pragma once
#include <string>
#include <string_view>

void erans_encode(std::string_view in, std::string& out);
void erans_decode(std::string_view in, std::string& out);

#define erans_maxsize (1ull<<24)
