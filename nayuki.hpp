#pragma once

#include <string>
#include <string_view>

void nayuki_static_encode(std::string_view in, std::string& out);
void nayuki_static_decode(std::string_view in, std::string& out);

void nayuki_adaptive_encode(std::string_view in, std::string& out);
void nayuki_adaptive_decode(std::string_view in, std::string& out);
