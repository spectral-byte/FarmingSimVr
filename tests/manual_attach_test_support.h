#pragma once

#ifndef HAND_TOOLS_PRE_FRAME_EXTRA_LUA
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA \
    "if StubIC and StubIC.tick then StubIC.tick() end; if StubManualAttach and StubManualAttach.tick then StubManualAttach.tick() end"
#endif

#include "animated_object_test_support.h"
#include <string>

namespace manual_attach_test {

using namespace animated_object_test;

inline std::string ManualAttachStubs(const char* variant = "ok") {
    return StubChunk("manual_attach") + "\n" + StubInstalled("manual_attach_vehicles", std::string("StubManualAttach.install('") + variant + "')");
}

constexpr const char* kTractorAhead = "_G.tractor = StubManualAttach.tractor({}); StubManualAttach.detectAll()";
// Where the ray of the hand at kHandX ends when it is on that rear hitch.
constexpr const char* kRearHitch = "-0.40 -0.20 2.00";
constexpr const char* kRearPtoStub = "-0.40 0.20 1.70";
constexpr const char* kRearHoseCoupler = "-0.05 -0.05 1.70";
constexpr const char* kRearCustomHoseCoupler = "-0.75 -0.05 1.70";
constexpr float kPtoHandY = 0.2f, kHoseHandX = 0.05f, kCustomHoseHandX = 0.75f, kHoseHandY = -0.05f;

class VirtualGamepadManualAttachTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    void StartWithManualAttach(const char* variant = "ok") {
        stubs_ = AnimatedObjectStubs() + ManualAttachStubs(variant);
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, StubChunk("animated_object_helpers"), "=animatedObjectHelpers"), 0);
    }

    void Lua(const std::string& chunk) { ASSERT_EQ(RunLuauChunk(L, chunk.c_str(), "=test"), 0) << chunk; }

    int Performs(const char* kind) { return static_cast<int>(Number(L, std::string("StubManualAttach.performCount('") + kind + "')")); }
    int Performs() { return static_cast<int>(Number(L, "#StubManualAttach.performs")); }
    int Warnings() { return CountLogLines("Manual Attach support disabled"); }
    int BlinkingWarnings() { return static_cast<int>(Number(L, "#StubManualAttach.warnings")); }
    std::string Attached(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".attacherVehicle ~= nil)"); }
    // "true" when the Implement's PTO / hoses are connected.
    std::string PtoConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".ptoConnected == true)"); }
    std::string HosesConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".hosesConnected == true)"); }

    void StartWithoutManualAttach() {
        stubs_ = AnimatedObjectStubs();
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, StubChunk("animated_object_helpers"), "=animatedObjectHelpers"), 0);
    }

    void NoteManualAttachFunctions() {
        Lua(R"luau(
            function _G.manualAttachFunctions()
                local list = {}
                local handler = StubManualAttach.handler
                for _, owner in ipairs({ StubManualAttach.Handler, StubManualAttach.instance, handler, handler.vehicleJointAttachment,
                        getmetatable(handler.vehicleJointAttachment), handler.powerTakeOffAttachment, handler.connectionHosesAttachment }) do
                    for key, value in pairs(owner) do
                        -- the candidate update is the one thing the module wraps (CandidateUpdateIsAsFound)
                        if type(value) == "function" and not (owner == StubManualAttach.Handler and key == "updateCandidates") then
                            list[tostring(owner) .. key] = value
                        end
                    end
                end
                return list
            end
            _G.functionsBefore = manualAttachFunctions()
        )luau");
    }
    bool CandidateUpdateIsAsFound() {
        return EvalLua(L, "tostring(StubManualAttach.Handler.updateCandidates == StubManualAttach.originalCandidateUpdate)") == "true";
    }
    bool ManualAttachFunctionsAreAsFound() {
        return EvalLua(L, R"luau((function()
            local after = manualAttachFunctions()
            for key, value in pairs(functionsBefore) do if after[key] ~= value then return 'false' end end
            for key, value in pairs(after) do if functionsBefore[key] ~= value then return 'false' end end
            return 'true'
        end)())luau") == "true";
    }

private:
    std::string stubs_;
};

}
