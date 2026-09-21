#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#if defined(FSR4_COMPRESSED_ASSETS)
#include "../third_party/zstd/zstd.h"
#endif

namespace fsr4assets {
// Applies to a single resource, not the whole bundle. Bound corrupt metadata
// before allocation; current resources are comfortably below this limit.
inline constexpr std::size_t max_asset_bytes=64u*1024u*1024u;
inline std::vector<std::uint8_t> decode(const std::uint8_t* bytes,
    std::size_t stored_size,std::size_t original_size,bool compressed) {
    if(!bytes || !stored_size || !original_size || stored_size>max_asset_bytes ||
       original_size>max_asset_bytes) throw std::runtime_error("invalid embedded asset size");
    if(!compressed) {
        if(stored_size!=original_size) throw std::runtime_error("raw embedded asset size mismatch");
        return {bytes,bytes+stored_size};
    }
#if defined(FSR4_COMPRESSED_ASSETS)
    if(ZSTD_getFrameContentSize(bytes,stored_size)!=original_size)
        throw std::runtime_error("compressed embedded asset size mismatch");
    const auto frame_size=ZSTD_findFrameCompressedSize(bytes,stored_size);
    if(ZSTD_isError(frame_size) || frame_size!=stored_size)
        throw std::runtime_error("invalid compressed embedded asset frame");
    std::vector<std::uint8_t> result(original_size);
    // The build enables frame checksums. ZSTD_decompress checks them as well as
    // stream structure, and never writes beyond this bounded output buffer.
    const auto decoded=ZSTD_decompress(result.data(),result.size(),bytes,stored_size);
    if(ZSTD_isError(decoded) || decoded!=original_size)
        throw std::runtime_error("cannot decompress embedded asset");
    return result;
#else
    throw std::runtime_error("compressed assets unavailable in raw debug build");
#endif
}
}
