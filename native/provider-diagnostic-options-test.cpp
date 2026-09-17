#include "../provider/diagnostic-options.hpp"
#include <cassert>
#include <iostream>
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
    std::cout << "provider diagnostic opt-in tests passed\n";
}
