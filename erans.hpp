#pragma once
#include "shrub.hpp"
#include "types.hpp"
#include <span>
#include <string>
#include <string_view>

#define erans_maxsize (1ull<<24)

// simple api
// the output buffers are resized as needed
void erans_encode_simple(std::string_view in, std::string& out);
void erans_decode_simple(std::string_view in, std::string& out);


// advanced api (decoder only for now)
struct erans_state {
    Shrub shrub;
    std::span<const u8> stream;

    // decodes the shrub, so the caller knows how much space is needed to decode
    // populates the stream
    void decode_shrub(std::string_view in); // XXX: make this a constructor?

    // out must have space for exactly shrub.size() bytes
    // decoding consumes the shrub
    void decode_to(std::span<u8> out);
};
