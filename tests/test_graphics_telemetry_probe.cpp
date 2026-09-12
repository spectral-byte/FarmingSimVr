#include <gtest/gtest.h>
#include "telemetry/graphics_telemetry_probe.h"

namespace fs25vr {

class GraphicsTelemetryProbeTest : public ::testing::Test {
protected:
    GraphicsTelemetryProbe probe;

    void SetUp() override {
        probe.Reset();
    }
};

TEST_F(GraphicsTelemetryProbeTest, RecordsSingleViewportAndScissorAndRenderTargetsOnFrame) {
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = 1920.0f;
    vp.Height = 1080.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor{};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 1920;
    scissor.bottom = 1080;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ 0x1000 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{ 0x2000 };

    probe.RecordViewports(1, &vp);
    probe.RecordScissorRects(1, &scissor);
    probe.RecordRenderTargets(1, &rtv, FALSE, &dsv);
    probe.OnFrameBoundary();

    EXPECT_EQ(probe.GetCurrentFrameIndex(), 1u);
    const auto& lastFrame = probe.GetLastFrameTelemetry();
    EXPECT_EQ(lastFrame.frameIndex, 1u);
    ASSERT_EQ(lastFrame.passes.size(), 1u);

    const auto& pass = lastFrame.passes[0];
    ASSERT_EQ(pass.viewports.size(), 1u);
    EXPECT_FLOAT_EQ(pass.viewports[0].width, 1920.0f);
    EXPECT_FLOAT_EQ(pass.viewports[0].height, 1080.0f);
    ASSERT_EQ(pass.scissorRects.size(), 1u);
    EXPECT_EQ(pass.scissorRects[0].right, 1920);
    EXPECT_EQ(pass.scissorRects[0].bottom, 1080);
    EXPECT_EQ(pass.rtvCount, 1u);
    EXPECT_TRUE(pass.hasDsv);
    EXPECT_EQ(lastFrame.maxConcurrentViewports, 1u);
}

TEST_F(GraphicsTelemetryProbeTest, RecordsMultipleViewportsAndDetectsMultiViewportStereo) {
    D3D12_VIEWPORT vps[2]{};
    // Left eye viewport
    vps[0].TopLeftX = 0.0f;
    vps[0].TopLeftY = 0.0f;
    vps[0].Width = 1920.0f;
    vps[0].Height = 1080.0f;
    vps[0].MinDepth = 0.0f;
    vps[0].MaxDepth = 1.0f;

    // Right eye viewport
    vps[1].TopLeftX = 1920.0f;
    vps[1].TopLeftY = 0.0f;
    vps[1].Width = 1920.0f;
    vps[1].Height = 1080.0f;
    vps[1].MinDepth = 0.0f;
    vps[1].MaxDepth = 1.0f;

    D3D12_RECT scissors[2]{};
    scissors[0].left = 0; scissors[0].top = 0; scissors[0].right = 1920; scissors[0].bottom = 1080;
    scissors[1].left = 1920; scissors[1].top = 0; scissors[1].right = 3840; scissors[1].bottom = 1080;

    probe.RecordViewports(2, vps);
    probe.RecordScissorRects(2, scissors);
    probe.OnFrameBoundary();

    const auto& lastFrame = probe.GetLastFrameTelemetry();
    EXPECT_EQ(lastFrame.maxConcurrentViewports, 2u);
    EXPECT_TRUE(lastFrame.hasMultiViewportStereo);
    EXPECT_TRUE(probe.HasNativeStereoDetected());
    EXPECT_FALSE(probe.RequiresAfrFallback());
    EXPECT_EQ(probe.GetStereoVerdict(), StereoVerdict::NativeStereoDetected);
}

TEST_F(GraphicsTelemetryProbeTest, DeterminesAfrFallbackRequiredWhenMonoscopicAcrossSuccessiveFrames) {
    D3D12_VIEWPORT vp{};
    vp.Width = 2560.0f;
    vp.Height = 1440.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor{ 0, 0, 2560, 1440 };

    // Record 5 successive single-viewport frames
    for (int i = 0; i < 5; ++i) {
        probe.RecordViewports(1, &vp);
        probe.RecordScissorRects(1, &scissor);
        probe.OnFrameBoundary();
    }

    EXPECT_EQ(probe.GetCurrentFrameIndex(), 5u);
    EXPECT_EQ(probe.GetMaxConcurrentViewportsObserved(), 1u);
    EXPECT_FALSE(probe.HasNativeStereoDetected());
    EXPECT_TRUE(probe.RequiresAfrFallback());
    EXPECT_EQ(probe.GetStereoVerdict(), StereoVerdict::AfrFallbackRequired);
}

TEST_F(GraphicsTelemetryProbeTest, DiagnosticReportContainsPassDetailsAndVerdict) {
    D3D12_VIEWPORT vp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, 1920, 1080 };
    probe.RecordViewports(1, &vp);
    probe.RecordScissorRects(1, &scissor);
    probe.OnFrameBoundary();

    std::string report = probe.GenerateDiagnosticReport();
    EXPECT_NE(report.find("Pass 0"), std::string::npos);
    EXPECT_NE(report.find("1920"), std::string::npos);
    EXPECT_NE(report.find("1080"), std::string::npos);
    EXPECT_NE(report.find("Verdict"), std::string::npos);
}

} // namespace fs25vr
