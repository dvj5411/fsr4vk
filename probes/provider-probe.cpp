// Deterministic D3D12/FidelityFX reference generator for VKD3D-Proton.
//
// The program enumerates providers, creates an FSR4 context, generates a small
// synthetic temporal scene, dispatches several frames, and writes tightly
// packed FP16 output images.  The resulting files are the oracle for the
// Vulkan-native implementation.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <limits>
#include <string>
#include <vector>

#include "ffx_api.h"
#include "dx12/ffx_api_dx12.h"
#include "ffx_upscale.h"
#include "../native/experimental-resolution.hpp"
#include "fsr411-int8-selection.hpp"

namespace {

FILE* g_log = nullptr;
UINT g_frameCount = 8;
bool g_nmsInputs = false;
bool g_externalExposure = false;
float g_exposureValues[2]{1.f,1.f};
float g_preExposureValues[2]{1.f,1.f};
bool g_resetSecondFrame = false;
bool g_hdrInputs = false;
bool g_dynamicResolution = false;
bool g_fsr411Int8 = false;
bool g_outputSentinelOne = false;
bool g_nonLinearColor = false;
uint32_t g_colorDispatchFlags = 0;
std::string g_outputRoot = "reference-output";

UINT kRenderWidth = fsr4experiment::render_width;
UINT kRenderHeight = fsr4experiment::render_height;
UINT g_secondRenderWidth = 0, g_secondRenderHeight = 0;
UINT kOutputWidth = 1920;
UINT kOutputHeight = 1080;

template <typename T>
void Release(T*& object)
{
    if (object) {
        object->Release();
        object = nullptr;
    }
}

void Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    if (g_log) {
        va_list fileArgs;
        va_copy(fileArgs, args);
        std::vfprintf(g_log, format, fileArgs);
        std::fflush(g_log);
        va_end(fileArgs);
    }
    std::vfprintf(stderr, format, args);
    std::fflush(stderr);
    va_end(args);
}

const char* ReturnCodeName(ffxReturnCode_t code)
{
    switch (code) {
    case FFX_API_RETURN_OK: return "FFX_API_RETURN_OK";
    case FFX_API_RETURN_ERROR: return "FFX_API_RETURN_ERROR";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "FFX_API_RETURN_ERROR_RUNTIME_ERROR";
    case FFX_API_RETURN_NO_PROVIDER: return "FFX_API_RETURN_NO_PROVIDER";
    case FFX_API_RETURN_ERROR_MEMORY: return "FFX_API_RETURN_ERROR_MEMORY";
    case FFX_API_RETURN_ERROR_PARAMETER: return "FFX_API_RETURN_ERROR_PARAMETER";
    case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE:
        return "FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE";
    default: return "unknown";
    }
}

std::string Utf8(const wchar_t* text)
{
    if (!text || !*text) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool CreateDevice(ID3D12Device** outDevice)
{
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        Log("CreateDXGIFactory1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }

    bool created = false;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        hr = factory->EnumAdapters1(index, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) {
            Log("EnumAdapters1(%u) failed: 0x%08lx\n", index, static_cast<unsigned long>(hr));
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        Log("Adapter %u: %s (vendor=0x%04x device=0x%04x, VRAM=%llu MiB)%s\n", index,
            Utf8(desc.Description).c_str(), desc.VendorId, desc.DeviceId,
            static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024ull * 1024ull)),
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " [software]" : "");

        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_ID3D12Device,
                                   reinterpret_cast<void**>(outDevice));
            if (SUCCEEDED(hr)) {
                Log("D3D12 device created on adapter %u.\n", index);
                created = true;
                adapter->Release();
                break;
            }
            Log("  D3D12CreateDevice failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        }
        adapter->Release();
    }

    factory->Release();
    return created;
}

bool CreateTexture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
                   D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState,
                   ID3D12Resource** output)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  initialState, nullptr, IID_ID3D12Resource,
                                                  reinterpret_cast<void**>(output));
    if (FAILED(hr)) {
        Log("CreateCommittedResource(%ux%u, format=%u) failed: 0x%08lx\n", width, height,
            static_cast<unsigned>(format), static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

bool CreateBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_STATES initialState, ID3D12Resource** output)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  initialState, nullptr, IID_ID3D12Resource,
                                                  reinterpret_cast<void**>(output));
    if (FAILED(hr)) {
        Log("Create buffer (%llu bytes, heap=%u) failed: 0x%08lx\n",
            static_cast<unsigned long long>(size), static_cast<unsigned>(heapType),
            static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

struct TransferBuffer
{
    ID3D12Resource* resource = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;

    ~TransferBuffer() { Release(resource); }
};

struct ReferenceResources
{
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* exposure = nullptr;
    ID3D12Resource* reactive = nullptr;
    ID3D12Resource* transparency = nullptr;
    ID3D12Resource* output = nullptr;
    HANDLE eventHandle = nullptr;

    ~ReferenceResources()
    {
        if (eventHandle) CloseHandle(eventHandle);
        Release(output);
        Release(transparency);
        Release(reactive);
        Release(exposure);
        Release(motion);
        Release(depth);
        Release(color);
        Release(fence);
        Release(commandList);
        Release(allocator);
        Release(queue);
    }
};

struct FrameInputs
{
    std::vector<uint16_t> color;
    std::vector<float> depth;
    std::vector<uint16_t> motion;
    std::vector<uint8_t> reactive;
    std::vector<uint8_t> transparency;
    float jitterX = 0.0f;
    float jitterY = 0.0f;
};

uint16_t FloatToHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;

    if (exponent <= 0) {
        if (exponent < -10) return static_cast<uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    mantissa += 0x1000u;
    if (mantissa & 0x800000u) {
        mantissa = 0;
        ++exponent;
        if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) |
                                 (mantissa >> 13));
}

float HalfToFloat(uint16_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
    uint32_t exponent = (value >> 10) & 0x1fu;
    uint32_t mantissa = value & 0x3ffu;
    uint32_t bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int unbiased = -14;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --unbiased;
            }
            mantissa &= 0x3ffu;
            bits = sign | (static_cast<uint32_t>(unbiased + 127) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

float Halton(UINT index, UINT base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

void GenerateFrameInputs(UINT frameIndex, FrameInputs& inputs)
{
    const size_t pixelCount = static_cast<size_t>(kRenderWidth) * kRenderHeight;
    inputs.color.resize(pixelCount * 4);
    inputs.depth.resize(pixelCount);
    inputs.motion.resize(pixelCount * 2);
    inputs.reactive.resize(pixelCount);
    inputs.transparency.resize(pixelCount);
    inputs.jitterX = Halton(frameIndex + 1, 2) - 0.5f;
    inputs.jitterY = Halton(frameIndex + 1, 3) - 0.5f;

    const float objectX = 180.0f + static_cast<float>(frameIndex) * 11.0f;
    const float objectY = 155.0f + static_cast<float>(frameIndex) * 5.0f;
    const float glassX = 850.0f - static_cast<float>(frameIndex) * 7.0f;
    const float glassY = 385.0f;

    for (UINT y = 0; y < kRenderHeight; ++y) {
        for (UINT x = 0; x < kRenderWidth; ++x) {
            const size_t pixel = static_cast<size_t>(y) * kRenderWidth + x;
            const float sampleX = static_cast<float>(x) + 0.5f + inputs.jitterX;
            const float sampleY = static_cast<float>(y) + 0.5f + inputs.jitterY;
            const float u = sampleX / static_cast<float>(kRenderWidth);
            const float v = sampleY / static_cast<float>(kRenderHeight);
            const bool checker = ((static_cast<int>(sampleX) / 12) ^
                                  (static_cast<int>(sampleY) / 12)) & 1;
            const bool movingObject = sampleX >= objectX && sampleX < objectX + 235.0f &&
                                      sampleY >= objectY && sampleY < objectY + 178.0f;
            const float glassDx = sampleX - glassX;
            const float glassDy = sampleY - glassY;
            const bool glassObject = glassDx * glassDx + glassDy * glassDy < 82.0f * 82.0f;
            const bool thinDiagonal = std::fabs(sampleY - (0.42f * sampleX + 65.0f)) < 0.75f;

            float red = 0.04f + 0.38f * u;
            float green = 0.07f + 0.34f * v;
            float blue = checker ? 0.22f : 0.10f;
            float depth = 0.82f + 0.12f * v;
            float motionX = 0.0f;
            float motionY = 0.0f;
            uint8_t reactive = 0;
            uint8_t transparency = 0;

            if (thinDiagonal) {
                red = green = blue = 0.95f;
                depth = 0.48f;
                reactive = 96;
            }
            if (movingObject) {
                const bool stripe = (static_cast<int>(sampleX - objectX) / 5) & 1;
                red = stripe ? 0.92f : 0.16f;
                green = stripe ? 0.18f : 0.86f;
                blue = 0.08f + 0.18f * v;
                depth = 0.24f;
                if (frameIndex > 0) {
                    motionX = -11.0f / static_cast<float>(kRenderWidth);
                    motionY = -5.0f / static_cast<float>(kRenderHeight);
                }
                const float edge = std::fmin(
                    std::fmin(sampleX - objectX, objectX + 235.0f - sampleX),
                    std::fmin(sampleY - objectY, objectY + 178.0f - sampleY));
                reactive = edge < 5.0f ? 255 : 36;
            }
            if (glassObject) {
                const float alpha = 0.45f;
                red = red * (1.0f - alpha) + 0.10f * alpha;
                green = green * (1.0f - alpha) + 0.70f * alpha;
                blue = blue * (1.0f - alpha) + 1.00f * alpha;
                depth = 0.40f;
                transparency = 210;
                reactive = 230;
                if (frameIndex > 0)
                    motionX = 7.0f / static_cast<float>(kRenderWidth);
            }

            inputs.color[pixel * 4 + 0] = FloatToHalf(red);
            inputs.color[pixel * 4 + 1] = FloatToHalf(green);
            inputs.color[pixel * 4 + 2] = FloatToHalf(blue);
            inputs.color[pixel * 4 + 3] = FloatToHalf(1.0f);
            inputs.depth[pixel] = g_nmsInputs ? 1.0f - depth : depth;
            inputs.motion[pixel * 2 + 0] = FloatToHalf(motionX);
            inputs.motion[pixel * 2 + 1] = FloatToHalf(motionY);
            inputs.reactive[pixel] = reactive;
            inputs.transparency[pixel] = transparency;
        }
    }
}

bool InitializeTransferBuffer(ID3D12Device* device, ID3D12Resource* texture,
                              D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state,
                              TransferBuffer& transfer)
{
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    device->GetCopyableFootprints(&desc, 0, 1, 0, &transfer.footprint,
                                  &transfer.rowCount, &transfer.rowBytes,
                                  &transfer.totalBytes);
    return CreateBuffer(device, transfer.totalBytes, heapType, state, &transfer.resource);
}

void Transition(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commandList->ResourceBarrier(1, &barrier);
}

bool UploadTexture(ID3D12GraphicsCommandList* commandList, ID3D12Resource* texture,
                   TransferBuffer& upload, const void* pixels, size_t sourceRowBytes)
{
    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    HRESULT hr = upload.resource->Map(0, &noRead, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) return false;
    const uint8_t* source = static_cast<const uint8_t*>(pixels);
    for (UINT row = 0; row < upload.rowCount; ++row) {
        std::memcpy(mapped + upload.footprint.Offset +
                        static_cast<size_t>(row) * upload.footprint.Footprint.RowPitch,
                    source + static_cast<size_t>(row) * sourceRowBytes, sourceRowBytes);
    }
    upload.resource->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = upload.resource;
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = upload.footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
    return true;
}

bool CreateDirectories(const std::string& path)
{
    if (path.empty()) return false;
    std::string current;
    for (size_t index = 0; index < path.size(); ++index) {
        const char character = path[index];
        current += character;
        if ((character == '/' || character == '\\') && current.size() > 1) {
            if (current.size() == 3 && current[1] == ':') continue;
            CreateDirectoryA(current.c_str(), nullptr);
        }
    }
    if (CreateDirectoryA(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        return true;
    Log("Could not create output directory %s (Win32 error %lu).\n", path.c_str(), GetLastError());
    return false;
}

std::string SafeName(const char* path)
{
    std::string name = path ? path : "unknown";
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    const size_t extension = name.find_last_of('.');
    if (extension != std::string::npos) name.erase(extension);
    for (char& character : name) {
        const bool allowed = (character >= 'a' && character <= 'z') ||
                             (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') || character == '-' ||
                             character == '_';
        if (!allowed) character = '_';
    }
    return name.empty() ? "unknown" : name;
}

uint64_t Fnv1a64(const uint8_t* bytes, size_t byteCount)
{
    uint64_t hash = 1469598103934665603ull;
    for (size_t index = 0; index < byteCount; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct OutputStatistics
{
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    uint64_t finiteValues = 0;
    uint64_t nonFiniteValues = 0;
};

bool WriteFrameOutputs(const std::string& directory, UINT frameIndex,
                       const std::vector<uint16_t>& pixels, OutputStatistics& statistics,
                       uint64_t& hash)
{
    char rawName[64]{};
    char previewName[64]{};
    std::snprintf(rawName, sizeof(rawName), "frame-%03u.rgba16f", frameIndex);
    std::snprintf(previewName, sizeof(previewName), "frame-%03u.ppm", frameIndex);
    const std::string rawPath = directory + "/" + rawName;
    const std::string previewPath = directory + "/" + previewName;

    FILE* raw = std::fopen(rawPath.c_str(), "wb");
    if (!raw) return false;
    const size_t written = std::fwrite(pixels.data(), sizeof(uint16_t), pixels.size(), raw);
    std::fclose(raw);
    if (written != pixels.size()) return false;

    hash = Fnv1a64(reinterpret_cast<const uint8_t*>(pixels.data()),
                   pixels.size() * sizeof(uint16_t));
    std::vector<uint8_t> preview(static_cast<size_t>(kOutputWidth) * kOutputHeight * 3);
    for (size_t pixel = 0; pixel < static_cast<size_t>(kOutputWidth) * kOutputHeight; ++pixel) {
        for (size_t channel = 0; channel < 4; ++channel) {
            const float value = HalfToFloat(pixels[pixel * 4 + channel]);
            if (std::isfinite(value)) {
                statistics.minimum = std::fmin(statistics.minimum, value);
                statistics.maximum = std::fmax(statistics.maximum, value);
                statistics.sum += value;
                ++statistics.finiteValues;
            } else {
                ++statistics.nonFiniteValues;
            }
            if (channel < 3) {
                const float finiteValue = std::isfinite(value) ? value : 0.0f;
                const float toneMapped = finiteValue / (1.0f + std::fmax(0.0f, finiteValue));
                const float encoded = std::pow(std::fmax(0.0f, toneMapped), 1.0f / 2.2f);
                preview[pixel * 3 + channel] = static_cast<uint8_t>(
                    std::fmin(255.0f, std::fmax(0.0f, encoded * 255.0f + 0.5f)));
            }
        }
    }

    FILE* ppm = std::fopen(previewPath.c_str(), "wb");
    if (!ppm) return false;
    std::fprintf(ppm, "P6\n%u %u\n255\n", kOutputWidth, kOutputHeight);
    const size_t previewWritten = std::fwrite(preview.data(), 1, preview.size(), ppm);
    std::fclose(ppm);
    return previewWritten == preview.size();
}

bool WaitForGpu(ReferenceResources& resources, UINT64 fenceValue)
{
    if (FAILED(resources.queue->Signal(resources.fence, fenceValue))) return false;
    if (resources.fence->GetCompletedValue() >= fenceValue) return true;
    if (FAILED(resources.fence->SetEventOnCompletion(fenceValue, resources.eventHandle)))
        return false;
    return WaitForSingleObject(resources.eventHandle, 30000) == WAIT_OBJECT_0;
}

ffxReturnCode_t DispatchReferenceSequence(PfnFfxDispatch dispatch, ffxContext* context,
                                          ID3D12Device* device, const char* dllPath,
                                          const char* providerName)
{
    ReferenceResources resources;
    TransferBuffer colorUpload;
    TransferBuffer depthUpload;
    TransferBuffer motionUpload;
    TransferBuffer exposureUpload;
    TransferBuffer reactiveUpload;
    TransferBuffer transparencyUpload;
    TransferBuffer outputUpload;
    TransferBuffer outputReadback;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_ID3D12CommandQueue,
                                             reinterpret_cast<void**>(&resources.queue));
    if (FAILED(hr)) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_ID3D12CommandAllocator,
                                         reinterpret_cast<void**>(&resources.allocator));
    if (FAILED(hr)) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, resources.allocator, nullptr,
                                    IID_ID3D12GraphicsCommandList,
                                    reinterpret_cast<void**>(&resources.commandList));
    if (FAILED(hr)) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

    if (!CreateTexture(device, kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       &resources.color) ||
        !CreateTexture(device, kRenderWidth, kRenderHeight, DXGI_FORMAT_R32_FLOAT,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       &resources.depth) ||
        !CreateTexture(device, kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       &resources.motion) ||
        !CreateTexture(device, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &resources.exposure) ||
        !CreateTexture(device, kRenderWidth, kRenderHeight, DXGI_FORMAT_R8_UNORM,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       &resources.reactive) ||
        !CreateTexture(device, kRenderWidth, kRenderHeight, DXGI_FORMAT_R8_UNORM,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       &resources.transparency) ||
        !CreateTexture(device, kOutputWidth, kOutputHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &resources.output)) {
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

    if (!InitializeTransferBuffer(device, resources.color, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, colorUpload) ||
        !InitializeTransferBuffer(device, resources.depth, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, depthUpload) ||
        !InitializeTransferBuffer(device, resources.motion, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, motionUpload) ||
        !InitializeTransferBuffer(device, resources.exposure, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, exposureUpload) ||
        !InitializeTransferBuffer(device, resources.reactive, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, reactiveUpload) ||
        !InitializeTransferBuffer(device, resources.transparency, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, transparencyUpload) ||
        !InitializeTransferBuffer(device, resources.output, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_STATE_GENERIC_READ, outputUpload) ||
        !InitializeTransferBuffer(device, resources.output, D3D12_HEAP_TYPE_READBACK,
                                  D3D12_RESOURCE_STATE_COPY_DEST, outputReadback)) {
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                             reinterpret_cast<void**>(&resources.fence));
    if (FAILED(hr)) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    resources.eventHandle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!resources.eventHandle) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

    const std::string outputDirectory = g_outputRoot + "/" + SafeName(dllPath);
    if (!CreateDirectories(outputDirectory)) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    const std::string metadataPath = outputDirectory + "/reference.json";
    FILE* metadata = std::fopen(metadataPath.c_str(), "wb");
    if (!metadata) {
        Log("Could not create %s.\n", metadataPath.c_str());
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    std::fprintf(metadata, "{\n  \"dynamic_resolution\": %s,\n", g_dynamicResolution ? "true" : "false");
    std::fprintf(metadata, "  \"output_sentinel\": %u,\n", g_outputSentinelOne ? 1u : 0u);
    std::fprintf(metadata, "  \"nonlinear_color\": %s,\n  \"color_dispatch_flags\": %u,\n",
                 g_nonLinearColor ? "true" : "false",g_colorDispatchFlags);
    std::fprintf(metadata,
                 "  \"schema\": 1,\n  \"provider\": \"%s\",\n"
                 "  \"render_size\": [%u, %u],\n  \"output_size\": [%u, %u],\n"
                 "  \"output_format\": \"little-endian-rgba16f\",\n  \"nms_inputs\": %s,\n  \"hdr_inputs\": %s,\n  \"frames\": [\n",
                 providerName ? providerName : "unknown", kRenderWidth, kRenderHeight,
                 kOutputWidth, kOutputHeight, g_nmsInputs ? "true" : "false", g_hdrInputs ? "true" : "false");

    ffxReturnCode_t result = FFX_API_RETURN_OK;
    bool firstMetadataFrame = true;
    const std::vector<uint16_t> clearedOutput(
        static_cast<size_t>(kOutputWidth) * kOutputHeight * 4, g_outputSentinelOne ? 0x3c00 : 0);
    for (UINT frameIndex = 0; frameIndex < g_frameCount; ++frameIndex) {
        if (frameIndex == 1 && g_secondRenderWidth) {
            kRenderWidth=g_secondRenderWidth; kRenderHeight=g_secondRenderHeight;
            Release(resources.color); Release(colorUpload.resource);
            if (!CreateTexture(device,kRenderWidth,kRenderHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,
                               D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&resources.color) ||
                !InitializeTransferBuffer(device,resources.color,D3D12_HEAP_TYPE_UPLOAD,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,colorUpload)) {
                result=FFX_API_RETURN_ERROR_RUNTIME_ERROR; break;
            }
            Release(resources.depth); Release(depthUpload.resource);
            if (!CreateTexture(device,kRenderWidth,kRenderHeight,DXGI_FORMAT_R32_FLOAT,
                               D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&resources.depth) ||
                !InitializeTransferBuffer(device,resources.depth,D3D12_HEAP_TYPE_UPLOAD,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,depthUpload)) {
                result=FFX_API_RETURN_ERROR_RUNTIME_ERROR; break;
            }
            Release(resources.motion); Release(motionUpload.resource);
            if (!CreateTexture(device,kRenderWidth,kRenderHeight,DXGI_FORMAT_R16G16_FLOAT,
                               D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&resources.motion) ||
                !InitializeTransferBuffer(device,resources.motion,D3D12_HEAP_TYPE_UPLOAD,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,motionUpload)) {
                result=FFX_API_RETURN_ERROR_RUNTIME_ERROR; break;
            }
            Release(resources.reactive); Release(reactiveUpload.resource);
            if (!CreateTexture(device,kRenderWidth,kRenderHeight,DXGI_FORMAT_R8_UNORM,
                               D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&resources.reactive) ||
                !InitializeTransferBuffer(device,resources.reactive,D3D12_HEAP_TYPE_UPLOAD,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,reactiveUpload)) {
                result=FFX_API_RETURN_ERROR_RUNTIME_ERROR; break;
            }
            Release(resources.transparency); Release(transparencyUpload.resource);
            if (!CreateTexture(device,kRenderWidth,kRenderHeight,DXGI_FORMAT_R8_UNORM,
                               D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,&resources.transparency) ||
                !InitializeTransferBuffer(device,resources.transparency,D3D12_HEAP_TYPE_UPLOAD,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,transparencyUpload)) {
                result=FFX_API_RETURN_ERROR_RUNTIME_ERROR; break;
            }
        }
        FrameInputs inputs;
        GenerateFrameInputs(frameIndex, inputs);

        if (frameIndex > 0) {
            if (FAILED(resources.allocator->Reset()) ||
                FAILED(resources.commandList->Reset(resources.allocator, nullptr))) {
                result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
                break;
            }
        }

        Transition(resources.commandList, resources.color,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.depth,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.motion,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.exposure,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.reactive,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.transparency,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(resources.commandList, resources.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST);

        const float exposureValue = g_exposureValues[frameIndex ? 1 : 0];
        if (!UploadTexture(resources.commandList, resources.color, colorUpload,
                           inputs.color.data(), static_cast<size_t>(kRenderWidth) * 8) ||
            !UploadTexture(resources.commandList, resources.depth, depthUpload,
                           inputs.depth.data(), static_cast<size_t>(kRenderWidth) * 4) ||
            !UploadTexture(resources.commandList, resources.motion, motionUpload,
                           inputs.motion.data(), static_cast<size_t>(kRenderWidth) * 4) ||
            !UploadTexture(resources.commandList, resources.exposure, exposureUpload,
                           &exposureValue, sizeof(exposureValue)) ||
            !UploadTexture(resources.commandList, resources.reactive, reactiveUpload,
                           inputs.reactive.data(), static_cast<size_t>(kRenderWidth)) ||
            !UploadTexture(resources.commandList, resources.transparency, transparencyUpload,
                           inputs.transparency.data(), static_cast<size_t>(kRenderWidth)) ||
            !UploadTexture(resources.commandList, resources.output, outputUpload,
                           clearedOutput.data(), static_cast<size_t>(kOutputWidth) * 8)) {
            result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
            break;
        }

        Transition(resources.commandList, resources.color, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.depth, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.motion, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.exposure, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.reactive, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.transparency, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(resources.commandList, resources.output, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ffxDispatchDescUpscale desc{};
        desc.flags = g_colorDispatchFlags;
        desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        desc.commandList = resources.commandList;
        desc.color = ffxApiGetResourceDX12(resources.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.depth = ffxApiGetResourceDX12(resources.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.motionVectors = ffxApiGetResourceDX12(resources.motion,
                                                   FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.exposure = ffxApiGetResourceDX12(resources.exposure,
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
        if (g_nmsInputs && !g_externalExposure) desc.exposure = {};
        desc.reactive = ffxApiGetResourceDX12(resources.reactive,
                                              FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.transparencyAndComposition = ffxApiGetResourceDX12(
            resources.transparency, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.output = ffxApiGetResourceDX12(resources.output,
                                            FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        desc.jitterOffset = {inputs.jitterX, inputs.jitterY};
        desc.motionVectorScale = {static_cast<float>(kRenderWidth),
                                  static_cast<float>(kRenderHeight)};
        desc.renderSize = {kRenderWidth, kRenderHeight};
        desc.upscaleSize = {kOutputWidth, kOutputHeight};
        desc.frameTimeDelta = 16.6667f;
        desc.preExposure = g_preExposureValues[frameIndex ? 1 : 0];
        desc.reset = frameIndex == 0 || (g_resetSecondFrame && frameIndex == 1);
        desc.cameraNear = 0.1f;
        desc.cameraFar = 1000.0f;
        desc.cameraFovAngleVertical = 1.0471976f;
        desc.viewSpaceToMetersFactor = 1.0f;

        Log("Dispatching reference frame %u/%u (jitter=%+.6f,%+.6f reset=%s)...\n",
            frameIndex + 1, g_frameCount, inputs.jitterX, inputs.jitterY,
            desc.reset ? "true" : "false");
        result = dispatch(context, &desc.header);
        if (result != FFX_API_RETURN_OK) {
            Log("Frame dispatch recording failed: %s (%u)\n", ReturnCodeName(result), result);
            break;
        }

        Transition(resources.commandList, resources.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION readbackDestination{};
        readbackDestination.pResource = outputReadback.resource;
        readbackDestination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readbackDestination.PlacedFootprint = outputReadback.footprint;
        D3D12_TEXTURE_COPY_LOCATION outputSource{};
        outputSource.pResource = resources.output;
        outputSource.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        resources.commandList->CopyTextureRegion(&readbackDestination, 0, 0, 0, &outputSource,
                                                 nullptr);
        Transition(resources.commandList, resources.output, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (FAILED(resources.commandList->Close())) {
            result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
            break;
        }
        ID3D12CommandList* lists[] = {resources.commandList};
        resources.queue->ExecuteCommandLists(1, lists);
        if (!WaitForGpu(resources, frameIndex + 1)) {
            Log("Timed out waiting for reference frame %u.\n", frameIndex);
            result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
            break;
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, static_cast<SIZE_T>(outputReadback.totalBytes)};
        if (FAILED(outputReadback.resource->Map(0, &readRange,
                                                reinterpret_cast<void**>(&mapped)))) {
            result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
            break;
        }
        std::vector<uint16_t> tightOutput(
            static_cast<size_t>(kOutputWidth) * kOutputHeight * 4);
        for (UINT row = 0; row < kOutputHeight; ++row) {
            std::memcpy(reinterpret_cast<uint8_t*>(tightOutput.data()) +
                            static_cast<size_t>(row) * kOutputWidth * 8,
                        mapped + outputReadback.footprint.Offset +
                            static_cast<size_t>(row) * outputReadback.footprint.Footprint.RowPitch,
                        static_cast<size_t>(kOutputWidth) * 8);
        }
        D3D12_RANGE noWrite{0, 0};
        outputReadback.resource->Unmap(0, &noWrite);

        OutputStatistics statistics;
        uint64_t hash = 0;
        if (!WriteFrameOutputs(outputDirectory, frameIndex, tightOutput, statistics, hash)) {
            Log("Failed to write output files for frame %u.\n", frameIndex);
            result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
            break;
        }
        const double mean = statistics.finiteValues
                                ? statistics.sum / static_cast<double>(statistics.finiteValues)
                                : 0.0;
        Log("Frame %u complete: fnv1a64=%016llx range=[%.7g, %.7g] mean=%.7g nonfinite=%llu\n",
            frameIndex, static_cast<unsigned long long>(hash), statistics.minimum,
            statistics.maximum, mean,
            static_cast<unsigned long long>(statistics.nonFiniteValues));
        if (!firstMetadataFrame) std::fprintf(metadata, ",\n");
        std::fprintf(metadata, "    {\"render_size\": [%u, %u], ", kRenderWidth,kRenderHeight);
        std::fprintf(metadata,
                     "\"index\": %u, \"jitter\": [%.9g, %.9g], \"reset\": %s, "
                     "\"raw\": \"frame-%03u.rgba16f\", \"preview\": \"frame-%03u.ppm\", "
                     "\"fnv1a64\": \"%016llx\", \"min\": %.9g, \"max\": %.9g, "
                     "\"mean\": %.12g, \"nonfinite\": %llu}",
                     frameIndex, inputs.jitterX, inputs.jitterY,
                     desc.reset ? "true" : "false", frameIndex, frameIndex,
                     static_cast<unsigned long long>(hash), statistics.minimum,
                     statistics.maximum, mean,
                     static_cast<unsigned long long>(statistics.nonFiniteValues));
        firstMetadataFrame = false;
    }

    std::fprintf(metadata, "\n  ]\n}\n");
    std::fclose(metadata);
    if (result == FFX_API_RETURN_OK)
        Log("Reference sequence written to %s.\n", outputDirectory.c_str());
    return result;
}

int ProbeDll(const char* path, ID3D12Device* device)
{
    Log("\n=== %s ===\n", path);
    HMODULE module = LoadLibraryA(path);
    if (!module) {
        Log("LoadLibraryA failed: Win32 error %lu\n", GetLastError());
        return 1;
    }

    auto query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"));
    if (g_fsr411Int8) {
        try {
            fsr411probe::select(module);
            Log("Research selection: embedded FSR 4.1.1 INT8 provider; FP8 disabled.\n");
        } catch (const std::exception& error) {
            Log("INT8 selection failed: %s\n", error.what());
            FreeLibrary(module);
            return 1;
        }
    }
    if (!query) {
        Log("The DLL does not export ffxQuery (Win32 error %lu).\n", GetLastError());
        FreeLibrary(module);
        return 1;
    }

    uint64_t count = 0;
    ffxQueryDescGetVersions request{};
    request.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    request.createDescType = 0x00010000u; // FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE
    request.device = device;
    request.outputCount = &count;

    ffxReturnCode_t rc = query(nullptr, &request.header);
    Log("Count query: %s (%u), providers=%llu\n", ReturnCodeName(rc), rc,
        static_cast<unsigned long long>(count));
    if (rc != FFX_API_RETURN_OK || count == 0) {
        FreeLibrary(module);
        return rc == FFX_API_RETURN_OK ? 0 : 2;
    }

    std::vector<uint64_t> ids(static_cast<size_t>(count));
    std::vector<const char*> names(static_cast<size_t>(count), nullptr);
    request.versionIds = ids.data();
    request.versionNames = names.data();
    uint64_t capacity = count;
    request.outputCount = &capacity;

    rc = query(nullptr, &request.header);
    Log("Enumeration query: %s (%u), providers=%llu\n", ReturnCodeName(rc), rc,
        static_cast<unsigned long long>(capacity));
    if (rc == FFX_API_RETURN_OK) {
        const size_t returned = static_cast<size_t>(capacity < count ? capacity : count);
        for (size_t index = 0; index < returned; ++index) {
            Log("  [%zu] id=0x%016llx name=%s\n", index,
                static_cast<unsigned long long>(ids[index]), names[index] ? names[index] : "(null)");
        }

        size_t fsr4Index = returned;
        for (size_t index = 0; index < returned; ++index) {
            if (names[index] && names[index][0] == '4' && names[index][1] == '.') {
                fsr4Index = index;
                break;
            }
        }

        if (fsr4Index == returned) {
            Log("No FSR4 provider exposed; context creation skipped.\n");
        } else {
            auto create = reinterpret_cast<PfnFfxCreateContext>(
                GetProcAddress(module, "ffxCreateContext"));
            auto destroy = reinterpret_cast<PfnFfxDestroyContext>(
                GetProcAddress(module, "ffxDestroyContext"));
            auto dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module, "ffxDispatch"));
            if (!create || !destroy || !dispatch) {
                Log("FSR4 is exposed, but one or more context entry points are missing.\n");
                rc = FFX_API_RETURN_ERROR;
            } else {
                ffxCreateContextDescUpscale upscale{};
                upscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
                // Default oracle uses external exposure. NMS-style mode captures
                // the combined inverted-depth and internal auto-exposure permutation.
                upscale.flags = g_nmsInputs ?
                    FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0;
                if (g_externalExposure) upscale.flags &= ~FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
                if (g_dynamicResolution) upscale.flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
                if (g_nonLinearColor) upscale.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
                if (g_hdrInputs) upscale.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
                upscale.maxRenderSize = {std::max(kRenderWidth,g_secondRenderWidth),
                                         std::max(kRenderHeight,g_secondRenderHeight)};
                upscale.maxUpscaleSize = {kOutputWidth, kOutputHeight};

                ffxCreateBackendDX12Desc backend{};
                backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
                backend.device = device;

                ffxOverrideVersion version{};
                version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
                version.versionId = ids[fsr4Index];

                upscale.header.pNext = &backend.header;
                // 4.1.1 FP8 and INT8 share a version ID. An override bypasses
                // CanProvide and chooses the first (FP8) instance, so let the
                // explicitly selected probe providers resolve this request.
                backend.header.pNext = g_fsr411Int8 ? nullptr : &version.header;

                ffxContext context = nullptr;
                Log("Creating %s context at %ux%u -> %ux%u...\n", names[fsr4Index], kRenderWidth, kRenderHeight, kOutputWidth, kOutputHeight);
                rc = create(&context, &upscale.header, nullptr);
                Log("Context creation: %s (%u), context=%p\n", ReturnCodeName(rc), rc, context);
                if (rc == FFX_API_RETURN_OK && context) {
                    if (g_fsr411Int8) {
                        auto provider=*reinterpret_cast<void***>(context);
                        auto table=*reinterpret_cast<void***>(provider);
                        auto expected=fsr411probe::provider_table(module,".?AVffxProvider_FSR4_Int8@@");
                        Log("Research provider vtable=%p expected INT8=%p\n",table,expected);
                        if (table!=expected) {
                            Log("Refusing capture: the selected context is not the embedded INT8 provider.\n");
                            destroy(&context,nullptr);
                            FreeLibrary(module);
                            return 2;
                        }
                    }
                    ffxReturnCode_t dispatchRc = DispatchReferenceSequence(
                        dispatch, &context, device, path, names[fsr4Index]);
                    ffxQueryGetProviderVersion selected{};
                    selected.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
                    ffxReturnCode_t queryRc = query(&context, &selected.header);
                    Log("Selected provider query: %s (%u), id=0x%016llx name=%s\n",
                        ReturnCodeName(queryRc), queryRc,
                        static_cast<unsigned long long>(selected.versionId),
                        selected.versionName ? selected.versionName : "(null)");

                    ffxReturnCode_t destroyRc = destroy(&context, nullptr);
                    Log("Context destruction: %s (%u)\n", ReturnCodeName(destroyRc), destroyRc);
                    if (dispatchRc != FFX_API_RETURN_OK || queryRc != FFX_API_RETURN_OK ||
                        destroyRc != FFX_API_RETURN_OK)
                        rc = FFX_API_RETURN_ERROR;
                }
            }
        }
    }

    FreeLibrary(module);
    return rc == FFX_API_RETURN_OK ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
    _dup2(_fileno(stderr), _fileno(stdout));
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_log = std::fopen("fsr4-provider-probe-result.txt", "wb");
    Log("FSR4 provider probe (x86-64 Windows)\n");
    Log("Pointer size: %zu bits\n", sizeof(void*) * 8);

    if (argc < 2) {
        Log("Usage: %s [--frames N] [--output-dir DIR] [--render-size WxH] [--output-size WxH] [--nms-inputs] [--external-exposure FIRST,SECOND] [--pre-exposure FIRST,SECOND] [--reset-second-frame] [--dynamic-resolution] [--fsr411-int8] [--output-sentinel-one] [--nonlinear-color|--srgb-color|--pq-color] <upscaler.dll> [upscaler.dll ...]\n",
            argv[0]);
        return 64;
    }

    std::vector<const char*> dllPaths;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            const long count = std::strtol(argv[++index], nullptr, 10);
            if (count < 1 || count > 64) {
                Log("--frames must be between 1 and 64.\n");
                return 64;
            }
            g_frameCount = static_cast<UINT>(count);
        } else if ((std::strcmp(argv[index], "--second-render-size") == 0 ||
                    std::strcmp(argv[index], "--render-size") == 0 ||
                    std::strcmp(argv[index], "--output-size") == 0) && index + 1 < argc) {
            const bool render = std::strcmp(argv[index], "--render-size") == 0;
            const bool second = std::strcmp(argv[index], "--second-render-size") == 0;
            unsigned width=0, height=0; char extra=0;
            if (std::sscanf(argv[++index], "%ux%u%c", &width, &height, &extra) != 2 ||
                width < 64 || height < 64 || width > 3840 || height > 2160) {
                Log("Invalid size; expected WIDTHxHEIGHT within 64x64..3840x2160.\n");
                return 64;
            }
            if (second) { g_secondRenderWidth=width; g_secondRenderHeight=height; }
            else if (render) { kRenderWidth=width; kRenderHeight=height; }
            else { kOutputWidth=width; kOutputHeight=height; }
        } else if (std::strcmp(argv[index], "--output-dir") == 0 && index + 1 < argc) {
            g_outputRoot = argv[++index];
        } else if (std::strcmp(argv[index], "--nms-inputs") == 0) {
            g_nmsInputs = true;
        } else if ((std::strcmp(argv[index], "--external-exposure") == 0 ||
                    std::strcmp(argv[index], "--pre-exposure") == 0) && index+1<argc) {
            const bool external=std::strcmp(argv[index], "--external-exposure") == 0;
            auto* values=external ? g_exposureValues : g_preExposureValues;
            char extra=0;
            if(std::sscanf(argv[++index],"%f,%f%c",&values[0],&values[1],&extra)!=2 ||
               !std::isfinite(values[0]) || !std::isfinite(values[1]) || values[0]<0 || values[1]<0 ||
               (!external && (values[0]==0 || values[1]==0))) {
                Log("Expected two finite exposure values: FIRST,SECOND (positive pre-exposure).\n");return 64;
            }
            if(external)g_externalExposure=true;
        } else if (std::strcmp(argv[index], "--reset-second-frame") == 0) {
            g_resetSecondFrame=true;
        } else if (std::strcmp(argv[index], "--dynamic-resolution") == 0) {
            g_dynamicResolution = true;
        } else if (std::strcmp(argv[index], "--fsr411-int8") == 0) {
            g_fsr411Int8 = true;
        } else if (std::strcmp(argv[index], "--output-sentinel-one") == 0) {
            g_outputSentinelOne = true;
        } else if (std::strcmp(argv[index], "--nonlinear-color") == 0) {
            g_nonLinearColor = true;
        } else if (std::strcmp(argv[index], "--srgb-color") == 0) {
            g_colorDispatchFlags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        } else if (std::strcmp(argv[index], "--pq-color") == 0) {
            g_colorDispatchFlags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
        } else if (std::strcmp(argv[index], "--hdr-inputs") == 0) {
            g_hdrInputs = true;
        } else if (argv[index][0] == '-') {
            Log("Unknown option: %s\n", argv[index]);
            return 64;
        } else {
            dllPaths.push_back(argv[index]);
        }
    }
    if (g_secondRenderWidth && (!g_dynamicResolution || g_frameCount!=2 ||
        g_secondRenderWidth>kOutputWidth || g_secondRenderHeight>kOutputHeight)) {
        Log("Second render size requires DRS, two frames, and input <= output.\n"); return 64;
    }
    if (kRenderWidth > kOutputWidth || kRenderHeight > kOutputHeight) {
        Log("Render size must not exceed output size.\n");
        return 64;
    }
    if (dllPaths.empty()) {
        Log("At least one upscaler DLL path is required.\n");
        return 64;
    }
    Log("Reference workload: %u frame(s), %ux%u -> %ux%u, output=%s\n",
        g_frameCount, kRenderWidth, kRenderHeight, kOutputWidth, kOutputHeight,
        g_outputRoot.c_str());

    ID3D12Device* device = nullptr;
    if (!CreateDevice(&device)) {
        Log("No hardware D3D12 device could be created.\n");
        return 3;
    }

    int failures = 0;
    for (const char* path : dllPaths) {
        if (ProbeDll(path, device) != 0) ++failures;
    }

    device->Release();
    Log("\nProbe complete: %d DLL(s) returned an error.\n", failures);
    if (g_log) std::fclose(g_log);
    return failures == 0 ? 0 : 4;
}
