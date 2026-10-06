#pragma once

#include <d3d12.h>
#include <dxgiformat.h>
#include <functional>
#include <memory>
#include <string>

namespace fs25vr {

// The motion vector pass recorded on the game's command list just ahead of an upscaler evaluation.
// It reads the engine's depth and motion vectors and writes motion vectors into a texture the
// Injector owns. The GPU work sits behind this interface so the interceptor is testable with a fake.
struct MvPassInputs {
    ID3D12GraphicsCommandList* cmdList = nullptr;
    // The engine's resources, read in the state the upscaler already requires (never transitioned).
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
};

struct MvPassResult {
    bool ok = false;
    // The Injector's motion vector texture, in the state the upscaler requires. Valid until the
    // upscaler evaluation that follows has been recorded.
    ID3D12Resource* output = nullptr;
    // Why the pass cannot run. Set when ok is false; the interceptor logs it and disables the pass.
    std::string failureReason;
};

class IMotionVectorPass {
public:
    virtual ~IMotionVectorPass() = default;
    virtual MvPassResult Record(const MvPassInputs& inputs) = 0;
};

// Creates the pass the first time an evaluation needs it. May return null, which disables the pass.
using MotionVectorPassFactory = std::function<std::unique_ptr<IMotionVectorPass>(ID3D12GraphicsCommandList*)>;

// Motion vector and depth formats the compute pass supports (it is a copy: output format = input format).
// Return the DXGI format a shader view reads them through, or DXGI_FORMAT_UNKNOWN when unsupported.
DXGI_FORMAT MvPassMotionVectorViewFormat(DXGI_FORMAT resourceFormat);
DXGI_FORMAT MvPassDepthViewFormat(DXGI_FORMAT resourceFormat);

// HLSL source of the pass-through compute shader.
const char* MvPassShaderSource();
// Compile the shader with the system's d3dcompiler_47.dll. Returns true and fills bytecode on success;
// otherwise fills error with the reason.
bool MvPassCompileShader(std::string& bytecode, std::string& error);

// The real D3D12 implementation. Resources are created lazily from the command list's device.
std::unique_ptr<IMotionVectorPass> CreateD3D12MotionVectorPass();

} // namespace fs25vr
