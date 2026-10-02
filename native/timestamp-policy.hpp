#pragma once
#include <cstdint>
#include <stdexcept>
namespace fsr4vk {
inline std::uint64_t timestamp_delta(std::uint64_t begin,std::uint64_t end,unsigned valid_bits) {
    if(!valid_bits || valid_bits>64) throw std::invalid_argument("timestampValidBits must be 1..64");
    const auto mask=valid_bits==64 ? ~std::uint64_t(0) : (std::uint64_t(1)<<valid_bits)-1;
    return (end-begin)&mask;
}
}
