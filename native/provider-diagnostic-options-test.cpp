#include "../provider/diagnostic-options.hpp"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    using fsr4vk::provider_log_requested;
    assert(!provider_log_requested(nullptr, nullptr));
    assert(!provider_log_requested("", nullptr));
    assert(!provider_log_requested("0", nullptr));
    assert(!provider_log_requested("false", nullptr));
    assert(!provider_log_requested("yes", nullptr));
    assert(!provider_log_requested(nullptr, ""));
    assert(provider_log_requested("1", nullptr));
    assert(provider_log_requested("1", ""));
    assert(provider_log_requested(nullptr, "Z:/tmp/provider.log"));
    assert(provider_log_requested("0", "Z:/tmp/provider.log"));
    using fsr4vk::default_provider_log_path;
    using std::filesystem::path;
    std::vector<path> attempts;
    const auto accept=[&](const path& p) {attempts.push_back(p);return true;};
    assert(default_provider_log_path("/games/NMS/NMS.exe","/temp",42,accept)==
           path("/games/NMS/fsr4vk-provider-42.log"));
    assert(attempts.size()==1);
    attempts.clear();
    const auto fallback=[&](const path& p) {attempts.push_back(p);return p.parent_path()=="/temp";};
    assert(default_provider_log_path("/readonly/NMS.exe","/temp",42,fallback)==
           path("/temp/fsr4vk-provider-42.log"));
    assert(attempts.size()==2);
    assert(default_provider_log_path({},"/temp",42,accept)==path("/temp/fsr4vk-provider-42.log"));
    assert(default_provider_log_path("/games/NMS/NMS.exe",{},42,[](const path&){return false;}).empty());
    assert(default_provider_log_path({}, {},42,accept).empty());
    std::cout << "provider diagnostic opt-in tests passed\n";
}
