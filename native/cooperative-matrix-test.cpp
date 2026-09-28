#include "../provider/cooperative-matrix.hpp"
#include <cassert>
#include <iostream>
#include <string>
namespace { bool valid_tuple=true,valid_wave=true; }
extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice,VkPhysicalDeviceProperties2* p) {
    p->properties.limits.maxComputeSharedMemorySize=65536;
    auto* size=static_cast<VkPhysicalDeviceSubgroupSizeControlProperties*>(p->pNext);
    size->minSubgroupSize=valid_wave?32:64;size->maxSubgroupSize=64;
    size->requiredSubgroupSizeStages=VK_SHADER_STAGE_COMPUTE_BIT;
    auto* subgroup=static_cast<VkPhysicalDeviceSubgroupProperties*>(size->pNext);
    subgroup->supportedOperations=VK_SUBGROUP_FEATURE_BALLOT_BIT;
}
VkResult VKAPI_CALL matrices(VkPhysicalDevice,uint32_t* count,VkCooperativeMatrixPropertiesKHR* out) {
    *count=1;if(!out)return VK_SUCCESS;
    out->MSize=out->NSize=out->KSize=16;
    out->AType=out->BType=valid_tuple?VK_COMPONENT_TYPE_SINT8_KHR:VK_COMPONENT_TYPE_FLOAT16_KHR;
    out->CType=out->ResultType=VK_COMPONENT_TYPE_SINT32_KHR;out->scope=VK_SCOPE_SUBGROUP_KHR;
    return VK_SUCCESS;
}
template<class F> void rejects(F f,const char* message) {
    bool failed=false;try{f();}catch(const std::runtime_error& e){failed=std::string(e.what()).find(message)!=std::string::npos;}
    assert(failed);
}
int main() {
    fsr4vk::validate_cooperative_matrix(VK_NULL_HANDLE,matrices,VK_TRUE);
    rejects([]{fsr4vk::validate_cooperative_matrix(VK_NULL_HANDLE,matrices,VK_FALSE);},"MISSING_DEVICE_FEATURES");
    rejects([]{fsr4vk::validate_cooperative_matrix(VK_NULL_HANDLE,nullptr,VK_TRUE);},"MISSING_DEVICE_FEATURES");
    valid_tuple=false;
    rejects([]{fsr4vk::validate_cooperative_matrix(VK_NULL_HANDLE,matrices,VK_TRUE);},"16x16x16");
    valid_tuple=true;valid_wave=false;
    rejects([]{fsr4vk::validate_cooperative_matrix(VK_NULL_HANDLE,matrices,VK_TRUE);},"wave32");
    std::cout<<"Cooperative matrix capability and enablement tests passed\n";
}
