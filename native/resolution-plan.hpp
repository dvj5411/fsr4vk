#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace fsr4 {
inline uint32_t round_up(uint32_t value,uint32_t multiple) { return (value+multiple-1)/multiple*multiple; }
inline uint32_t divide_up(uint32_t value,uint32_t divisor) { return (value+divisor-1)/divisor; }
inline float sanitize_pre_exposure(float value) {
    return std::isfinite(value) && value>0.0f ? value : 1.0f;
}
inline const char* resolution_preset(uint32_t input_width,uint32_t output_width) {
    const float ratio=float(round_up(output_width,8))/input_width;
    if(ratio>=2.99f)return "ultraperf";
    if(ratio>=1.99f)return "performance";
    if(ratio>=1.69f)return "balanced";
    // Original 4.0.2c capture at 900 -> 1344 uses Quality (ratio 1.4933).
    if(ratio>=1.49f)return "quality";
    return "native";
}
inline uint64_t scratch_capacity(uint32_t width,uint32_t height) {
    return width>1920 || height>1080 ? 83232256ull : 20880256ull;
}
inline void validate_dimensions(uint32_t iw,uint32_t ih,uint32_t ow,uint32_t oh) {
    if(iw<64 || ih<64 || ow<64 || oh<64 || ow>3840 || oh>2160 || iw>ow || ih>oh)
        throw std::invalid_argument("supported dimension bounds: 64..3840 x 64..2160, input <= output");
}
inline std::array<uint32_t,2> quality_dispatch(uint32_t pass,uint32_t width,uint32_t height) {
    width=round_up(width,8);height=round_up(height,8);
    if(pass==0 || pass==13)return {divide_up(width,16),divide_up(height,16)};
    if(pass==9)return {divide_up(width/8,8),divide_up(height/8,8)};
    const auto divisor=(pass<=2 || pass==12) ? 2u : (pass<=5 || pass>=10) ? 4u : 8u;
    return {divide_up(width/divisor,64),height/divisor};
}
}
