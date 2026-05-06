#pragma once
#include <string>
#include <string_view>

static inline void erans_encode(std::string_view in, std::string& out) { }
static inline void erans_decode(std::string_view in, std::string& out) { }

#define erans_maxsize (24*1024*1024)
