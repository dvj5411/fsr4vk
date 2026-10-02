#include "resource-adoption.hpp"
#include <new>
#include <stdexcept>
#include <iostream>
#include <vector>
struct Owner {
    bool fail=false;
    std::vector<int> handles;
    void push_back(int handle) {if(fail) throw std::bad_alloc();handles.push_back(handle);}
};
int main() {
    Owner owner; int destroyed=0;
    fsr4core::adopt_resource(owner,42,[&](int){++destroyed;});
    if(owner.handles.size()!=1 || destroyed) throw std::runtime_error("adopted resource destroyed");
    owner.fail=true;
    try{fsr4core::adopt_resource(owner,43,[&](int handle){if(handle!=43)throw std::runtime_error("wrong resource");++destroyed;});}
    catch(const std::bad_alloc&){}
    if(destroyed!=1 || owner.handles.size()!=1)throw std::runtime_error("failed adoption leaked resource");
    std::cout << "resource ownership adoption failure tests passed\n";
}
