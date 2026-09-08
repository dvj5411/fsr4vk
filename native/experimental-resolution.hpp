#pragma once
// Deliberately compile-time and off by default. Output strides in the captured
// Quality model remain specialized for 1920x1080; this only probes input scaling.
namespace fsr4experiment {
#if defined(FSR4_EXPERIMENTAL_540P_INPUT) && defined(FSR4_EXPERIMENTAL_648P_INPUT)
#error Select only one experimental input resolution
#endif
#if defined(FSR4_EXPERIMENTAL_540P_INPUT)
inline constexpr unsigned render_width = 960, render_height = 540;
#elif defined(FSR4_EXPERIMENTAL_648P_INPUT)
inline constexpr unsigned render_width = 1152, render_height = 648;
#else
inline constexpr unsigned render_width = 1280, render_height = 720;
#endif
}
