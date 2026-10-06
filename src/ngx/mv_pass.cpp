#include "mv_pass.h"
#include "core/logger.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <sstream>
#include <utility>
#include <vector>
#include <windows.h>

namespace fs25vr {

using Microsoft::WRL::ComPtr;

DXGI_FORMAT MvPassMotionVectorViewFormat(DXGI_FORMAT resourceFormat) {
    switch (resourceFormat) {
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_TYPELESS:
            return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_TYPELESS:
            return DXGI_FORMAT_R32G32_FLOAT;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT MvPassDepthViewFormat(DXGI_FORMAT resourceFormat) {
    switch (resourceFormat) {
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

const char* MvPassShaderSource() {
    // Pass-through: the output is the engine's motion vector, unchanged. Depth is bound already so the
    // later correction (which unprojects from it) needs no new plumbing.
    return R"hlsl(
Texture2D<float>  gDepth   : register(t0);
Texture2D<float2> gMv      : register(t1);
RWTexture2D<float2> gOut   : register(u0);

cbuffer Constants : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint gPad0;
    uint gPad1;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;
    gOut[id.xy] = gMv.Load(int3(id.xy, 0));
}
)hlsl";
}

bool MvPassCompileShader(std::string& bytecode, std::string& error) {
    using PFN_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                            LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) {
        error = "d3dcompiler_47.dll could not be loaded";
        return false;
    }
    auto compile = reinterpret_cast<PFN_D3DCompile>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) {
        error = "d3dcompiler_47.dll has no D3DCompile";
        FreeLibrary(compiler);
        return false;
    }
    const char* source = MvPassShaderSource();
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> messages;
    HRESULT hr = compile(source, std::char_traits<char>::length(source), "fs25vr_mv_pass.hlsl", nullptr, nullptr,
                         "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
    bool ok = SUCCEEDED(hr) && code;
    if (ok) {
        bytecode.assign(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
    } else {
        std::stringstream ss;
        ss << "shader compile failed (0x" << std::hex << static_cast<unsigned long>(hr) << ")";
        if (messages) ss << ": " << std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize());
        error = ss.str();
    }
    code.Reset();
    messages.Reset();
    // The compiler stays loaded for the process: its COM objects were released above, and a later
    // Record never needs it again, but unloading buys nothing.
    return ok;
}

namespace {

constexpr UINT kSlotCount = 64;       // descriptor sets in flight: two eyes per frame, a few frames deep
constexpr UINT kDescriptorsPerSlot = 3; // depth SRV, motion vector SRV, output UAV
constexpr uint64_t kRetireCalls = 64; // a replaced output stays alive this many Records for in-flight frames
constexpr D3D12_RESOURCE_STATES kUpscalerInputState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

class D3D12MotionVectorPass : public IMotionVectorPass {
public:
    MvPassResult Record(const MvPassInputs& in) override {
        MvPassResult fail;
        if (!in.cmdList || !in.depth || !in.motionVectors) {
            fail.failureReason = "missing command list, depth or motion vector resource";
            return fail;
        }
        const D3D12_RESOURCE_DESC mvDesc = in.motionVectors->GetDesc();
        const D3D12_RESOURCE_DESC depthDesc = in.depth->GetDesc();
        if (mvDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || mvDesc.SampleDesc.Count != 1 ||
            depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || depthDesc.SampleDesc.Count != 1) {
            fail.failureReason = "depth and motion vectors must be single-sample 2D textures";
            return fail;
        }
        const DXGI_FORMAT mvFormat = MvPassMotionVectorViewFormat(mvDesc.Format);
        if (mvFormat == DXGI_FORMAT_UNKNOWN) {
            fail.failureReason = "unsupported motion vector format " + std::to_string(static_cast<int>(mvDesc.Format));
            return fail;
        }
        const DXGI_FORMAT depthFormat = MvPassDepthViewFormat(depthDesc.Format);
        if (depthFormat == DXGI_FORMAT_UNKNOWN) {
            fail.failureReason = "unsupported depth format " + std::to_string(static_cast<int>(depthDesc.Format));
            return fail;
        }
        if (mvDesc.Width == 0 || mvDesc.Height == 0 || mvDesc.Width > 16384 || mvDesc.Height > 16384) {
            fail.failureReason = "unexpected motion vector dimensions";
            return fail;
        }

        ComPtr<ID3D12Device> device;
        if (FAILED(in.cmdList->GetDevice(IID_PPV_ARGS(&device))) || !device) {
            fail.failureReason = "the command list has no device";
            return fail;
        }
        if (m_device && m_device.Get() != device.Get()) {
            fail.failureReason = "the D3D12 device changed";
            return fail;
        }
        if (!m_device) {
            std::string reason;
            if (!CreatePipeline(device.Get(), reason)) {
                fail.failureReason = reason;
                return fail;
            }
            m_device = device;
        }

        const UINT width = static_cast<UINT>(mvDesc.Width);
        const UINT height = mvDesc.Height;
        if (!m_output || m_width != width || m_height != height || m_format != mvFormat) {
            std::string reason;
            if (!CreateOutput(width, height, mvFormat, reason)) {
                fail.failureReason = reason;
                return fail;
            }
        }
        ++m_calls;
        ReleaseRetired();

        // Descriptors for this call: a ring slot, so the two eyes recorded in one frame do not overwrite
        // each other before the GPU has run them.
        const UINT slot = static_cast<UINT>(m_calls % kSlotCount);
        const UINT inc = m_descriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * kDescriptorsPerSlot * inc;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(slot) * kDescriptorsPerSlot * inc;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Format = depthFormat;
        device->CreateShaderResourceView(in.depth, &srv, cpu);
        cpu.ptr += inc;
        srv.Format = mvFormat;
        device->CreateShaderResourceView(in.motionVectors, &srv, cpu);
        cpu.ptr += inc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Format = mvFormat;
        device->CreateUnorderedAccessView(m_output.Get(), nullptr, &uav, cpu);

        ID3D12GraphicsCommandList* cl = in.cmdList;
        // Our own texture: shader resource (as the upscaler reads it) -> UAV for the write -> back.
        // The engine's depth and motion vectors are only read, in the state the upscaler already needs.
        Transition(cl, m_output.Get(), m_outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetComputeRootSignature(m_rootSignature.Get());
        cl->SetPipelineState(m_pso.Get());
        cl->SetComputeRootDescriptorTable(0, gpu);
        const UINT constants[4] = {width, height, 0, 0};
        cl->SetComputeRoot32BitConstants(1, 4, constants, 0);
        cl->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        Transition(cl, m_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kUpscalerInputState);
        m_outputState = kUpscalerInputState;

        MvPassResult ok;
        ok.ok = true;
        ok.output = m_output.Get();
        return ok;
    }

private:
    static void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                           D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        if (from == to) return;
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &b);
    }

    bool CreatePipeline(ID3D12Device* device, std::string& reason) {
        std::string bytecode, error;
        if (!MvPassCompileShader(bytecode, error)) {
            reason = error;
            return false;
        }

        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 2;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 2;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 0;
        params[1].Constants.Num32BitValues = 4;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 2;
        rsDesc.pParameters = params;

        ComPtr<ID3DBlob> rsBlob, rsErrors;
        HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErrors);
        if (FAILED(hr)) {
            reason = "root signature serialization failed (" + HrString(hr) + ")";
            return false;
        }
        hr = device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                         IID_PPV_ARGS(&m_rootSignature));
        if (FAILED(hr)) {
            reason = "root signature creation failed (" + HrString(hr) + ")";
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_rootSignature.Get();
        pso.CS.pShaderBytecode = bytecode.data();
        pso.CS.BytecodeLength = bytecode.size();
        hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_pso));
        if (FAILED(hr)) {
            reason = "compute pipeline creation failed (" + HrString(hr) + ")";
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = kSlotCount * kDescriptorsPerSlot;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_heap));
        if (FAILED(hr)) {
            reason = "descriptor heap creation failed (" + HrString(hr) + ")";
            return false;
        }
        m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    bool CreateOutput(UINT width, UINT height, DXGI_FORMAT format, std::string& reason) {
        if (m_output) m_retired.emplace_back(std::move(m_output), m_calls);

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = m_device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, kUpscalerInputState,
                                                       nullptr, IID_PPV_ARGS(&m_output));
        if (FAILED(hr)) {
            reason = "motion vector texture creation failed (" + HrString(hr) + ")";
            return false;
        }
        m_output->SetName(L"FS25VR corrected motion vectors");
        m_outputState = kUpscalerInputState;
        m_width = width;
        m_height = height;
        m_format = format;
        std::stringstream ss;
        ss << "MvPass: motion vector texture " << width << "x" << height << " format " << static_cast<int>(format);
        LogInfo(ss.str());
        return true;
    }

    void ReleaseRetired() {
        while (!m_retired.empty() && m_retired.front().second + kRetireCalls < m_calls) {
            m_retired.erase(m_retired.begin());
        }
    }

    static std::string HrString(HRESULT hr) {
        std::stringstream ss;
        ss << "0x" << std::hex << static_cast<unsigned long>(hr);
        return ss.str();
    }

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT m_descriptorSize = 0;
    ComPtr<ID3D12Resource> m_output;
    D3D12_RESOURCE_STATES m_outputState = kUpscalerInputState;
    UINT m_width = 0;
    UINT m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    uint64_t m_calls = 0;
    std::vector<std::pair<ComPtr<ID3D12Resource>, uint64_t>> m_retired;
};

} // namespace

std::unique_ptr<IMotionVectorPass> CreateD3D12MotionVectorPass() {
    return std::make_unique<D3D12MotionVectorPass>();
}

} // namespace fs25vr
