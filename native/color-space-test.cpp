#include "color-space.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace fsr4core;
    assert(select_color_space(false,false,false)==ColorSpace::Linear);
    assert(select_color_space(true,false,false)==ColorSpace::Nonlinear);
    assert(select_color_space(true,true,false)==ColorSpace::Srgb);
    assert(select_color_space(false,true,false)==ColorSpace::Srgb);
    assert(select_color_space(false,false,true)==ColorSpace::Pq);
    assert(select_color_space(true,true,true)==ColorSpace::Pq);
    bool rejected=false;
    try { color_space_name(static_cast<ColorSpace>(4)); } catch(...) { rejected=true; }
    assert(rejected);
    std::cout << "color-space selection tests passed\n";
}
