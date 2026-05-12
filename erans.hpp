#pragma once
#include "shrub.hpp"
#include "types.hpp"
#include <span>
#include <string>
#include <string_view>

#define erans_maxsize (1ull<<24)

// simple api
// the output buffers are resized as needed
void erans_encode(std::string_view in, std::string& out);
void erans_decode(std::string_view in, std::string& out);


// advanced api

// decodes the shrub first, so the caller knows how much space is needed to decode
// returns rans stream data
std::span<const u8> erans_decode_shrub(std::span<const u8> in, Shrub& shrub);

// out must have space for exactly shrub.size() bytes
// decoding consumes shrub
void erans_decode_to(std::span<const u8> rans, Shrub& shrub, std::span<u8> out);
