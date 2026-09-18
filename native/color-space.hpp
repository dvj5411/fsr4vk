#pragma once
#include <cstdint>
#include <stdexcept>

namespace fsr4core {
enum class ColorSpace : std::uint32_t { Linear, Nonlinear, Srgb, Pq };
inline const char* color_space_name(ColorSpace color) {
    switch(color) {
    case ColorSpace::Linear: return "linear";
    case ColorSpace::Nonlinear: return "nonlinear";
    case ColorSpace::Srgb: return "srgb";
    case ColorSpace::Pq: return "pq";
    }
    throw std::invalid_argument("unknown color space");
}
// Match SDK GetColorspace: explicit PQ > explicit sRGB > generic nonlinear.
inline ColorSpace select_color_space(bool nonlinear, bool srgb, bool pq) {
    return pq ? ColorSpace::Pq : srgb ? ColorSpace::Srgb :
           nonlinear ? ColorSpace::Nonlinear : ColorSpace::Linear;
}
}
