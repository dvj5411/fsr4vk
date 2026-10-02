#pragma once
#include <utility>

namespace fsr4core {
// The newly created handle is still ours if container allocation fails.
template<class Container,class Resource,class Cleanup>
void adopt_resource(Container& owner,Resource resource,Cleanup cleanup) {
    try { owner.push_back(resource); }
    catch(...) { cleanup(resource); throw; }
}
}
