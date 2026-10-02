#include "FFXVkInputReads.h"
#include <iostream>
#include <stdexcept>
struct Resource {int value;};
struct Parameters {
    Resource resource{7};
    bool fail=true,explicit_null=false;
    int Get(const char*,void** output) {
        if(fail) return 1; // The real missing-key contract leaves output unwritten.
        *output=explicit_null ? nullptr : &resource;return 0;
    }
    int Get(const char*,unsigned* output) {if(fail)return 1;*output=1;return 0;}
};
int main() {
    Parameters params;
    for(const auto* key:{"color","motion","output"}) {
        Resource* output=reinterpret_cast<Resource*>(1);
        if(FFXVkInputReads::Required(&params,key,0,output) || output)
            throw std::runtime_error("missing required input was dereferenceable");
    }
    if(FFXVkInputReads::Optional(&params,"reset",0,0u)!=0) throw std::runtime_error("reset default lost");
    params.fail=false;
    Resource* output=nullptr;
    if(!FFXVkInputReads::Required(&params,"color",0,output) || output!=&params.resource ||
       FFXVkInputReads::Optional(&params,"reset",0,0u)!=1) throw std::runtime_error("valid input lost");
    params.explicit_null=true;
    if(FFXVkInputReads::Required(&params,"color",0,output)) throw std::runtime_error("explicit null accepted");
    std::cout<<"host input-boundary tests passed\n";
}
