#include "research-memory-placement.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace fsr4memorytest;
    VkPhysicalDeviceMemoryProperties p{};
    p.memoryTypeCount=4;
    p.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    p.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    p.memoryTypes[2].propertyFlags=required;
    p.memoryTypes[3].propertyFlags=required|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    assert(choose(p,15,2,false)==2); // Control preserves current first-compatible policy.
    assert(choose(p,15,2,true)==3);
    assert(choose(p,7,2,true)==2); // Local candidate incompatible with this buffer.
    auto absent=p; absent.memoryTypes[3].propertyFlags=0;
    assert(choose(absent,15,2,true)==2);
    assert(choose(p,8,3,true)==3); // UMA/already-local case must not change placement.
    auto noncoherent=p; noncoherent.memoryTypes[3].propertyFlags &= ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    assert(choose(noncoherent,15,2,true)==2);
    for (auto baseline : {0u,1u,4u,32u}) {
        bool rejected=false;
        try { (void)choose(p,15,baseline,true); } catch (const std::runtime_error&) { rejected=true; }
        assert(rejected);
    }
    uint32_t selected{}; bool fallback{};
    std::vector<uint32_t> calls;
    auto result=attempt(2,3,[&](auto type) {
        calls.push_back(type); return type==3 ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
    },selected,fallback);
    assert(result==VK_SUCCESS && selected==2 && fallback && (calls==std::vector<uint32_t>{3,2}));
    for (auto failure : {VK_ERROR_DEVICE_LOST,VK_ERROR_OUT_OF_HOST_MEMORY}) {
        calls.clear();
        result=attempt(2,3,[&](auto type) { calls.push_back(type); return failure; },selected,fallback);
        assert(result==failure && selected==3 && !fallback && calls.size()==1);
    }
    calls.clear();
    result=attempt(2,2,[&](auto type) {calls.push_back(type);return VK_ERROR_OUT_OF_DEVICE_MEMORY;},selected,fallback);
    assert(result==VK_ERROR_OUT_OF_DEVICE_MEMORY && !fallback && calls.size()==1);
    calls.clear();
    result=attempt(2,3,[&](auto type) {calls.push_back(type);return VK_ERROR_OUT_OF_DEVICE_MEMORY;},selected,fallback);
    assert(result==VK_ERROR_OUT_OF_DEVICE_MEMORY && fallback && calls.size()==2);
    calls.clear();
    result=attempt(2,3,[&](auto type) {calls.push_back(type);return VK_SUCCESS;},selected,fallback);
    assert(result==VK_SUCCESS && selected==3 && !fallback && calls.size()==1);
    std::cout << "memory placement policy and failure fallback checks passed\n";
}
