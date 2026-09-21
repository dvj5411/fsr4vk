#include "../native/asset-codec.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=4) throw std::runtime_error("usage: asset-codec-probe COMPRESSED ORIGINAL_SIZE FILE");
        std::ifstream file(argv[3],std::ios::binary);
        if(!file) throw std::runtime_error("cannot open input");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
        const auto result=fsr4assets::decode(bytes.data(),bytes.size(),std::stoull(argv[2]),std::string(argv[1])=="1");
        std::cout.write(reinterpret_cast<const char*>(result.data()),result.size());
        return std::cout ? 0 : 1;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 2;
    }
}
