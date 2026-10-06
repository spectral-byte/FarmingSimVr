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
    // "main" is the pass-through: the output is the engine's motion vector, unchanged. Depth is bound
    // already so the later correction (which unprojects from it) needs no new plumbing.
    // "selfcheck" mirrors mv_reprojection.cpp (EvaluateSample): per sample on a sparse grid it unprojects
    // the pixel from reverse-Z depth through the current camera, projects it through the previous rendered
    // frame's camera and writes the error of the engine's vector (times the MV scale DLSS receives)
    // against that prediction, under both sign conventions. It writes only to the results buffer.
    return R"hlsl(
Texture2D<float>  gDepth   : register(t0);
Texture2D<float2> gMv      : register(t1);
RWTexture2D<float2> gOut   : register(u0);
RWStructuredBuffer<uint4> gResults : register(u1);

cbuffer Constants : register(b0)
{
    uint4  gA;  // width, height, unused, sequence
    float4 gB;  // mvScaleX, mvScaleY, tanHalfFovY (current), aspect
    float4 gC;  // near, far, grid dimension, tanHalfFovY (previous)
    float4 gR0; // rows of p_prev = R * p + c: xyz = R row, w = c
    float4 gR1;
    float4 gR2;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gA.x || id.y >= gA.y)
        return;
    gOut[id.xy] = gMv.Load(int3(id.xy, 0));
}

[numthreads(8, 8, 1)]
void selfcheck(uint3 id : SV_DispatchThreadID)
{
    uint grid = (uint)gC.z;
    if (id.x >= grid || id.y >= grid)
        return;
    float w = (float)gA.x;
    float h = (float)gA.y;
    uint px = min(gA.x - 1, (uint)(((float)id.x + 0.5) * w / (float)grid));
    uint py = min(gA.y - 1, (uint)(((float)id.y + 0.5) * h / (float)grid));

    uint4 result = uint4(0, 0, 0, gA.w); // errToPrevious, errToCurrent, status, sequence
    float depth = gDepth.Load(int3(px, py, 0));
    if (!(depth > 0.0))
    {
        result.z = 1; // sky
    }
    else
    {
        float n = gC.x;
        float f = gC.y;
        float z = n * f / (n + depth * (f - n));
        float cx = (float)px + 0.5;
        float cy = (float)py + 0.5;
        float3 p = float3((2.0 * cx / w - 1.0) * gB.z * gB.w * z, (1.0 - 2.0 * cy / h) * gB.z * z, -z);
        float3 q = float3(dot(gR0.xyz, p) + gR0.w, dot(gR1.xyz, p) + gR1.w, dot(gR2.xyz, p) + gR2.w);
        float zq = -q.z;
        float ppx = 0.0;
        float ppy = 0.0;
        bool onScreen = zq > 1e-6;
        if (onScreen)
        {
            ppx = (q.x / (zq * gC.w * gB.w) * 0.5 + 0.5) * w;
            ppy = (0.5 - q.y / (zq * gC.w) * 0.5) * h;
            onScreen = ppx >= 0.0 && ppx < w && ppy >= 0.0 && ppy < h;
        }
        if (!onScreen)
        {
            result.z = 2; // off-screen or behind the previous camera
        }
        else
        {
            float2 mv = gMv.Load(int3(px, py, 0)) * gB.xy;
            float2 d = float2(ppx - cx, ppy - cy);
            result.x = asuint(length(mv - d));
            result.y = asuint(length(mv + d));
        }
    }
    gResults[id.y * grid + id.x] = result;
}
)hlsl";
}
bool MvPassCompileShader(std::string& bytecode, std::string& error, const char* entryPoint) {
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
                         entryPoint, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
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
constexpr UINT kDescriptorsPerSlot = 4; // depth SRV, motion vector SRV, output UAV, self-check results UAV
constexpr UINT kSelfCheckGrid = 32;     // samples per side of the self-check grid
constexpr UINT kSelfCheckSamples = kSelfCheckGrid * kSelfCheckGrid;
constexpr UINT kReadbackSlots = 8;      // self-checks in flight before a readback buffer is reused
constexpr uint64_t kReadbackDelayCalls = 6;   // Records after which a self-check is read back (a few frames)
constexpr uint64_t kReadbackGiveUpCalls = 64; // a readback that never shows the expected stamp is dropped
// Root constants, laid out as the shader's cbuffer (24 dwords).
struct PassConstants {
    uint32_t a[4];    // width, height, unused, sequence
    float b[4];       // mvScaleX, mvScaleY, tanHalfFovY (current), aspect
    float c[4];       // near, far, grid, tanHalfFovY (previous)
    float r[3][4];    // p_prev = R * p + c per row
};
static_assert(sizeof(PassConstants) == 24 * sizeof(uint32_t), "root constants are 24 dwords");
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
        cpu.ptr += inc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC resultsUav = {};
        resultsUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        resultsUav.Format = DXGI_FORMAT_UNKNOWN;
        resultsUav.Buffer.NumElements = kSelfCheckSamples;
        resultsUav.Buffer.StructureByteStride = 4 * sizeof(uint32_t);
        device->CreateUnorderedAccessView(m_results.Get(), nullptr, &resultsUav, cpu);

        ID3D12GraphicsCommandList* cl = in.cmdList;
        // Our own texture: shader resource (as the upscaler reads it) -> UAV for the write -> back.
        // The engine's depth and motion vectors are only read, in the state the upscaler already needs.
        Transition(cl, m_output.Get(), m_outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetComputeRootSignature(m_rootSignature.Get());
        cl->SetPipelineState(m_pso.Get());
        cl->SetComputeRootDescriptorTable(0, gpu);
        PassConstants constants = {};
        constants.a[0] = width;
        constants.a[1] = height;
        cl->SetComputeRoot32BitConstants(1, sizeof(constants) / sizeof(uint32_t), &constants, 0);
        cl->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        Transition(cl, m_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kUpscalerInputState);
        m_outputState = kUpscalerInputState;

        // The self-check only measures: it reads the engine's depth and vectors and writes the results
        // buffer, which is copied to a readback buffer that is read a few evaluations later.
        if (in.selfCheck && EnsureReadbackBuffers()) RecordSelfCheck(cl, *in.selfCheck, width, height, gpu);

        MvPassResult ok;
        ok.ok = true;
        ok.output = m_output.Get();
        return ok;
    }

    std::vector<MvSelfCheckReport> PollSelfCheck() override {
        std::vector<MvSelfCheckReport> reports;
        for (PendingCheck& p : m_pending) {
            if (!p.active || m_calls < p.recordedAtCall + kReadbackDelayCalls) continue;
            D3D12_RANGE readRange = {0, kSelfCheckSamples * sizeof(mvreproj::SampleResult)};
            void* mapped = nullptr;
            if (FAILED(m_readback[p.slot]->Map(0, &readRange, &mapped)) || !mapped) {
                p.active = false;
                continue;
            }
            const auto* samples = static_cast<const mvreproj::SampleResult*>(mapped);
            // Every element carries the stamp of the evaluation that wrote it; a mismatch means the GPU
            // has not run that copy yet (or a later one overwrote it), so look again later.
            bool ready = true;
            for (UINT i = 0; i < kSelfCheckSamples && ready; ++i) ready = samples[i].sequence == p.sequence;
            if (ready) {
                MvSelfCheckReport report;
                report.eye = p.eye;
                report.evalIndex = p.evalIndex;
                report.stats = mvreproj::Summarize(samples, kSelfCheckSamples);
                reports.push_back(report);
                p.active = false;
            } else if (m_calls > p.recordedAtCall + kReadbackGiveUpCalls) {
                p.active = false;
            }
            D3D12_RANGE written = {0, 0};
            m_readback[p.slot]->Unmap(0, &written);
        }
        return reports;
    }

private:
    struct PendingCheck {
        bool active = false;
        UINT slot = 0;
        uint32_t sequence = 0;
        uint32_t eye = 0;
        uint64_t evalIndex = 0;
        uint64_t recordedAtCall = 0;
    };

    bool EnsureReadbackBuffers() {
        if (m_readbackReady) return true;
        if (m_readbackFailed) return false;
        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kSelfCheckSamples * sizeof(mvreproj::SampleResult);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (UINT i = 0; i < kReadbackSlots; ++i) {
            HRESULT hr = m_device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&m_readback[i]));
            if (FAILED(hr)) {
                // The self-check is a diagnostic: losing it must not cost the pass itself.
                LogWarn("MvPass: self-check readback buffer creation failed (" + HrString(hr) + "); the self-check is off");
                m_readbackFailed = true;
                return false;
            }
        }
        m_readbackReady = true;
        return true;
    }

    void RecordSelfCheck(ID3D12GraphicsCommandList* cl, const MvSelfCheckRequest& req, UINT width, UINT height,
                         D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        const UINT slot = static_cast<UINT>(m_checks % kReadbackSlots);
        ++m_checks;
        const uint32_t sequence = static_cast<uint32_t>(m_checks); // never 0: stale or empty reads never match

        PassConstants c = {};
        c.a[0] = width;
        c.a[1] = height;
        c.a[3] = sequence;
        c.b[0] = req.mvScaleX;
        c.b[1] = req.mvScaleY;
        c.b[2] = req.current.tanHalfFovY;
        c.b[3] = static_cast<float>(width) / static_cast<float>(height); // the render aspect
        c.c[0] = req.current.nearClip;
        c.c[1] = req.current.farClip;
        c.c[2] = static_cast<float>(kSelfCheckGrid);
        c.c[3] = req.previous.tanHalfFovY;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) c.r[row][col] = req.toPrevious.m[row][col];
            c.r[row][3] = req.toPrevious.c[row];
        }

        cl->SetPipelineState(m_selfCheckPso.Get());
        cl->SetComputeRootDescriptorTable(0, gpu);
        cl->SetComputeRoot32BitConstants(1, sizeof(c) / sizeof(uint32_t), &c, 0);
        cl->Dispatch((kSelfCheckGrid + 7) / 8, (kSelfCheckGrid + 7) / 8, 1);
        Transition(cl, m_results.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cl->CopyResource(m_readback[slot].Get(), m_results.Get());
        Transition(cl, m_results.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        PendingCheck& p = m_pending[slot];
        p.active = true;
        p.slot = slot;
        p.sequence = sequence;
        p.eye = req.eye;
        p.evalIndex = req.evalIndex;
        p.recordedAtCall = m_calls;
    }

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
        ranges[1].NumDescriptors = 2;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 2;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 0;
        params[1].Constants.Num32BitValues = sizeof(PassConstants) / sizeof(uint32_t);
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

        std::string checkBytecode;
        if (!MvPassCompileShader(checkBytecode, error, "selfcheck")) {
            reason = error;
            return false;
        }
        pso.CS.pShaderBytecode = checkBytecode.data();
        pso.CS.BytecodeLength = checkBytecode.size();
        hr = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_selfCheckPso));
        if (FAILED(hr)) {
            reason = "self-check pipeline creation failed (" + HrString(hr) + ")";
            return false;
        }

        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC buf = {};
        buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf.Width = kSelfCheckSamples * sizeof(mvreproj::SampleResult);
        buf.Height = 1;
        buf.DepthOrArraySize = 1;
        buf.MipLevels = 1;
        buf.SampleDesc.Count = 1;
        buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buf.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &buf,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_results));
        if (FAILED(hr)) {
            reason = "self-check results buffer creation failed (" + HrString(hr) + ")";
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
    ComPtr<ID3D12PipelineState> m_selfCheckPso;
    ComPtr<ID3D12Resource> m_results;
    ComPtr<ID3D12Resource> m_readback[kReadbackSlots];
    PendingCheck m_pending[kReadbackSlots];
    bool m_readbackReady = false;
    bool m_readbackFailed = false;
    uint64_t m_checks = 0;
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
