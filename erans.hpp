#pragma once
#include <string>
#include <string_view>

void erans_encode(std::string_view in, std::string& out);
void erans_decode(std::string_view in, std::string& out);

#define erans_stream_count 4
#define erans_stream_maxsize (1ull<<24)
#define erans_maxsize (erans_stream_count * erans_stream_maxsize)
