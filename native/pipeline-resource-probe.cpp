// Compile the production QualityContext pipelines without submitting GPU work.
// Only this translation unit intercepts pipeline creation for inspection.
#include <vulkan/vulkan.h>
#include <map>
#include <vector>
static std::map<VkShaderModule,std::vector<uint32_t>> compiled_module_inputs;
static VkResult track_shader_module(VkDevice device,const VkShaderModuleCreateInfo* info,
    const VkAllocationCallbacks* alloc,VkShaderModule* output) {
    const auto result=vkCreateShaderModule(device,info,alloc,output);
    if(result==VK_SUCCESS) compiled_module_inputs[*output]=
        std::vector<uint32_t>(info->pCode,info->pCode+info->codeSize/4);
    return result;
}
static VkResult inspect_create_pipelines(VkDevice, VkPipelineCache, uint32_t,
    const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
#define vkCreateComputePipelines inspect_create_pipelines
#define vkCreateShaderModule track_shader_module
#include "quality-context.hpp"
#undef vkCreateComputePipelines
#undef vkCreateShaderModule
#include "../provider/vulkan_device_features.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
uint32_t requested_wave = 0, pipeline_index = 0;
int selected_pass = -1;
bool capture = false;
std::filesystem::path ir_dir;
PFN_vkGetPipelineExecutablePropertiesKHR get_properties{};
PFN_vkGetPipelineExecutableStatisticsKHR get_statistics{};
PFN_vkGetPipelineExecutableInternalRepresentationsKHR get_ir{};

std::string quoted(const char* text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : std::string(text)) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"';
    return out.str();
}

template<class T, class F>
std::vector<T> enumerate(VkStructureType type, F query) {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t count = 0;
        fsr4core::check(query(&count, static_cast<T*>(nullptr)), "enumerate count");
        std::vector<T> values(count);
        for (auto& v : values) v.sType = type;
        const VkResult result = query(&count, values.data());
        if (result == VK_INCOMPLETE) continue;
        fsr4core::check(result, "enumerate values");
        values.resize(count);
        return values;
    }
    throw std::runtime_error("unstable executable enumeration");
}

void inspect(VkDevice device, VkPipeline pipeline, uint32_t pass, uint32_t wave) {
    VkPipelineInfoKHR info{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    info.pipeline = pipeline;
    auto properties = enumerate<VkPipelineExecutablePropertiesKHR>(
        VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR,
        [&](uint32_t* n, auto* p) { return get_properties(device, &info, n, p); });
    for (uint32_t i = 0; i < properties.size(); ++i) {
        const auto& p = properties[i];
        std::cout << "{\"kind\":\"executable\",\"pass\":" << pass
                  << ",\"index\":" << i << ",\"requested_wave\":" << wave
                  << ",\"actual_wave\":" << p.subgroupSize << ",\"name\":" << quoted(p.name) << "}\n";
        VkPipelineExecutableInfoKHR executable{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        executable.pipeline = pipeline;
        executable.executableIndex = i;
        auto stats = enumerate<VkPipelineExecutableStatisticKHR>(
            VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR,
            [&](uint32_t* n, auto* s) { return get_statistics(device, &executable, n, s); });
        for (const auto& s : stats) {
            std::cout << "{\"kind\":\"stat\",\"pass\":" << pass << ",\"index\":" << i
                      << ",\"name\":" << quoted(s.name) << ",\"description\":" << quoted(s.description)
                      << ",\"format\":" << s.format << ",\"value\":";
            switch (s.format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: std::cout << (s.value.b32 ? "true" : "false"); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: std::cout << s.value.i64; break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: std::cout << s.value.u64; break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: std::cout << std::setprecision(17) << s.value.f64; break;
            default: throw std::runtime_error("unknown statistic format");
            }
            std::cout << "}\n";
        }
        if (ir_dir.empty()) continue;
        bool complete = false;
        for (unsigned attempt = 0; attempt < 8 && !complete; ++attempt) {
            auto representations = enumerate<VkPipelineExecutableInternalRepresentationKHR>(
                VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR,
                [&](uint32_t* n, auto* r) { return get_ir(device, &executable, n, r); });
            std::vector<std::vector<char>> data(representations.size());
            for (size_t j = 0; j < data.size(); ++j) {
                if (representations[j].dataSize > 64 * 1024 * 1024)
                    throw std::runtime_error("oversized internal representation");
                data[j].resize(representations[j].dataSize);
                representations[j].pData = data[j].data();
            }
            uint32_t count = representations.size();
            const auto result = get_ir(device, &executable, &count, representations.data());
            if (result == VK_INCOMPLETE) continue;
            fsr4core::check(result, "get internal representation");
            if (count > representations.size()) throw std::runtime_error("invalid IR count");
            for (uint32_t j = 0; j < count; ++j) {
                auto& r = representations[j];
                if (r.dataSize > data[j].size()) throw std::runtime_error("invalid IR size");
                const auto filename = "pass-" + std::to_string(pass) + "-exec-" + std::to_string(i)
                    + "-ir-" + std::to_string(j) + (r.isText ? ".txt" : ".bin");
                std::ofstream out(ir_dir / filename, std::ios::binary);
                out.write(data[j].data(), r.dataSize);
                if (!out) throw std::runtime_error("cannot save IR");
                std::cout << "{\"kind\":\"ir\",\"pass\":" << pass << ",\"name\":" << quoted(r.name)
                          << ",\"description\":" << quoted(r.description) << ",\"file\":" << quoted(filename.c_str()) << "}\n";
            }
            complete = true;
        }
        if (!complete) throw std::runtime_error("unstable IR payload");
    }
}
} // namespace

static VkResult inspect_create_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo* original, const VkAllocationCallbacks* alloc, VkPipeline* pipelines) {
    if (count != 1) throw std::runtime_error("unexpected production pipeline batch");
    const uint32_t pass = pipeline_index == 14 ? 0 : pipeline_index + 1;
    ++pipeline_index;
    auto info = *original;
    if(!ir_dir.empty()) {
        const auto& words=compiled_module_inputs.at(info.stage.module);
        const auto filename=ir_dir/("pass-"+std::to_string(pass)+"-input.spv");
        std::ofstream file(filename,std::ios::binary);
        file.write(reinterpret_cast<const char*>(words.data()),words.size()*4);
        if(!file) throw std::runtime_error("cannot retain compiled module input");
        std::cout << "{\"kind\":\"input\",\"pass\":" << pass << ",\"file\":" << quoted(filename.string().c_str()) << "}\n";
    }
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
    const uint32_t wave = selected_pass < 0 || selected_pass == int(pass) ? requested_wave : 0;
    if (wave) {
        subgroup.requiredSubgroupSize = wave;
        subgroup.pNext = const_cast<void*>(info.stage.pNext);
        info.stage.pNext = &subgroup;
    }
    if (capture) info.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    if (!ir_dir.empty()) info.flags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
    std::cout << "{\"kind\":\"compile\",\"pass\":" << pass << ",\"requested_wave\":" << wave << "}\n" << std::flush;
    const auto result = vkCreateComputePipelines(device, cache, count, &info, alloc, pipelines);
    if (result == VK_SUCCESS && capture) {
        try { inspect(device, pipelines[0], pass, wave); }
        catch (...) { vkDestroyPipeline(device, pipelines[0], alloc); pipelines[0] = VK_NULL_HANDLE; throw; }
    }
    return result;
}

int main(int argc, char** argv) {
    VkInstance instance{};
    VkDevice device{};
    try {
        if (argc < 2 || argc > 5) throw std::runtime_error("usage: probe BUNDLE [0|32|64] [IR_DIR|-] [PASS|-1]");
        const std::filesystem::path bundle = argv[1];
        requested_wave = argc > 2 ? std::stoul(argv[2]) : 0;
        if (requested_wave != 0 && requested_wave != 32 && requested_wave != 64)
            throw std::runtime_error("invalid requested wave");
        if (argc > 3 && std::string(argv[3]) != "-") ir_dir = argv[3];
        if (argc > 4) selected_pass = std::stoi(argv[4]);
        if (selected_pass < -1 || selected_pass > 14) throw std::runtime_error("invalid pass");
        if (!ir_dir.empty()) {
            if (std::filesystem::exists(ir_dir)) throw std::runtime_error("IR directory already exists");
            std::filesystem::create_directories(ir_dir);
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "FSR4 pipeline resource probe";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app;
        fsr4core::check(vkCreateInstance(&ci, nullptr, &instance), "create instance");
        uint32_t count = 0;
        fsr4core::check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical devices");
        if (!count) throw std::runtime_error("no physical device");
        std::vector<VkPhysicalDevice> physical(count);
        fsr4core::check(vkEnumeratePhysicalDevices(instance, &count, physical.data()), "physical devices");
        if (count != 1) throw std::runtime_error("select exactly one Vulkan physical device externally");
        VkPhysicalDeviceSubgroupSizeControlProperties subgroup_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES}; driver.pNext = &subgroup_props;
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext = &driver;
        vkGetPhysicalDeviceProperties2(physical[0], &props);
        std::cout << "{\"kind\":\"device\",\"name\":" << quoted(props.properties.deviceName)
                  << ",\"driver\":" << quoted(driver.driverName) << ",\"driver_info\":" << quoted(driver.driverInfo)
                  << ",\"device_id\":" << props.properties.deviceID << ",\"api\":" << props.properties.apiVersion
                  << ",\"min_wave\":" << subgroup_props.minSubgroupSize << ",\"max_wave\":" << subgroup_props.maxSubgroupSize << "}\n";
        VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR exe_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
        subgroup_features.pNext = &exe_features;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &subgroup_features;
        vkGetPhysicalDeviceFeatures2(physical[0], &features);
        if (requested_wave && (!subgroup_features.subgroupSizeControl || requested_wave < subgroup_props.minSubgroupSize ||
            requested_wave > subgroup_props.maxSubgroupSize || !(subgroup_props.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT)))
            throw std::runtime_error("requested subgroup unsupported");
        uint32_t ext_count = 0;
        fsr4core::check(vkEnumerateDeviceExtensionProperties(physical[0], nullptr, &ext_count, nullptr), "extensions");
        std::vector<VkExtensionProperties> ext(ext_count);
        fsr4core::check(vkEnumerateDeviceExtensionProperties(physical[0], nullptr, &ext_count, ext.data()), "extensions");
        capture = exe_features.pipelineExecutableInfo && std::any_of(ext.begin(),ext.end(),[](auto& e) {
            return std::string(e.extensionName) == VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
        });
        if (!capture && !ir_dir.empty()) throw std::runtime_error("executable IR capture unsupported");
        std::cout << "{\"kind\":\"capture_support\",\"available\":" << (capture ? "true" : "false") << "}\n";
        uint32_t queues = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &queues, nullptr);
        std::vector<VkQueueFamilyProperties> qp(queues); vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &queues, qp.data());
        uint32_t family = 0;
        while (family < queues && !(qp[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        if (family == queues) throw std::runtime_error("no compute queue");
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        const char* extension = VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
        exe_features.pipelineExecutableInfo = capture;
        subgroup_features.subgroupSizeControl = requested_wave != 0;
        subgroup_features.computeFullSubgroups = VK_FALSE;
        subgroup_features.pNext = capture ? &exe_features : nullptr;
        di.pNext = &subgroup_features;
        if (capture) { di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &extension; }
        fsr4vk::DeviceFeatures enabled(physical[0],di,app.apiVersion,vkGetPhysicalDeviceFeatures2,vkEnumerateDeviceExtensionProperties);
        fsr4core::check(vkCreateDevice(physical[0],enabled.get(),nullptr,&device), "create device");
        if (capture) {
            get_properties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(vkGetDeviceProcAddr(device,"vkGetPipelineExecutablePropertiesKHR"));
            get_statistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(vkGetDeviceProcAddr(device,"vkGetPipelineExecutableStatisticsKHR"));
            get_ir = reinterpret_cast<PFN_vkGetPipelineExecutableInternalRepresentationsKHR>(vkGetDeviceProcAddr(device,"vkGetPipelineExecutableInternalRepresentationsKHR"));
            if (!get_properties || !get_statistics || !get_ir) throw std::runtime_error("missing executable entry points");
        }
        const bool large = bundle.parent_path().filename() == "2160";
        { fsr4core::QualityContext context(physical[0], device, bundle, bundle/"initializers.bin", bundle/"weights.bin",
            true, vkGetDeviceProcAddr, large ? 3840 : 1920, large ? 2160 : 1080,
            large ? 3840 : 1920, large ? 2160 : 1080, app.apiVersion); }
        if (pipeline_index != 15) throw std::runtime_error("unexpected production pipeline count");
        vkDestroyDevice(device,nullptr); device = {};
        vkDestroyInstance(instance,nullptr); instance = {};
        std::cout << "{\"kind\":\"complete\",\"pipelines\":15,\"gpu_dispatches\":0}\n";
        return 0;
    } catch (const std::exception& e) {
        if (device) vkDestroyDevice(device,nullptr);
        if (instance) vkDestroyInstance(instance,nullptr);
        std::cerr << "resource probe: " << e.what() << '\n';
        return 1;
    }
}
