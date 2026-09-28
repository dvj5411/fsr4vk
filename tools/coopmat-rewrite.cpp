#include "../native/coopmat-rewrite.hpp"
#include "../native/fsr411-quad-bounds.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=4)throw std::runtime_error("usage: coopmat-rewrite INPUT.spv OUTPUT.spv [--prepass-quads]");
        std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
        if(!in || in.tellg()%4)throw std::runtime_error("invalid input");
        std::vector<uint32_t> code(static_cast<size_t>(in.tellg())/4);
        in.seekg(0);in.read(reinterpret_cast<char*>(code.data()),code.size()*4);
        unsigned count=0;
        if(argc==4 && std::string(argv[3])=="--prepass-quads")fsr4::guard_fsr411_prepass_quads(code);
        else if(argc==3)count=fsr4coop::rewrite(code);
        else throw std::runtime_error("unknown option");
        std::ofstream out(argv[2],std::ios::binary);
        out.write(reinterpret_cast<const char*>(code.data()),code.size()*4);
        if(!out)throw std::runtime_error("output write failed");
        std::cout<<count<<"\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
