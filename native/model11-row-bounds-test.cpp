#include "model11-row-bounds.hpp"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: model11-row-bounds-test INPUT OUTPUT");
        std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
        const auto bytes=in.tellg();
        if(!in || bytes<=0 || bytes%4) throw std::runtime_error("invalid input size");
        std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4);
        in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),bytes);
        if(!in) throw std::runtime_error("short read");
        const bool changed=fsr4::guard_model11_rows(words);
        std::ofstream out(argv[2],std::ios::binary);
        out.write(reinterpret_cast<const char*>(words.data()),words.size()*4);
        if(!out) throw std::runtime_error("write failed");
        std::cout<<(changed ? "guarded" : "aligned-unchanged")<<'\n';
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
