// What the Manual Attach tests (tests/test_manual_attach*.cpp, ADR-0035, spec #227, ticket #228) share, on top of animated_object_test_support.h (the
// player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): a stand-in for the
// Manual Attach mod (FS25_manualAttach, release 1.1.0.0, shaped after its src/ files), the vehicles it works on as scene nodes, and a fixture.
// Header-only (inline), in its own namespace; a test file brings it in with `using namespace manual_attach_test;` inside its anonymous namespace.
//
// The stand-in is two chunks (manual_attach.luau, manual_attach_vehicles.luau), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs
// (ManualAttachStubs(variant)): they run after the Companion Mod's files are loaded and before the map is, so the mod finds Manual Attach when
// it looks. The vehicles are made after the scene exists. The game of a frame runs StubManualAttach.tick() (Manual Attach's own update: the
// "can attach by hand" flag from the player, and its handler's candidate update); this header defines HAND_TOOLS_PRE_FRAME_EXTRA_LUA to do so
// (and Interactive Control's update of its Click Points, when a test has them), so it is included FIRST by a test file.
//
// What the stand-in offers (StubManualAttach.install, tick, detect, ...) is described at the top of tests/stubs/manual_attach.luau, and
// the vehicles (vehicle, tractor, implement, lineUp, attach, ...) at the top of tests/stubs/manual_attach_vehicles.luau.

#pragma once

// What the game does each frame before the Companion Mod's update: Interactive Control's update of its Click Points (when a test has them) and
// Manual Attach's own update.
#ifndef HAND_TOOLS_PRE_FRAME_EXTRA_LUA
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA \
    "if StubIC and StubIC.tick then StubIC.tick() end; if StubManualAttach and StubManualAttach.tick then StubManualAttach.tick() end"
#endif

#include "animated_object_test_support.h"
#include <string>

namespace manual_attach_test {

using namespace animated_object_test;

// Manual Attach (manual_attach.luau and manual_attach_vehicles.luau), for StartOptions::extraStubs. variant: see the top of manual_attach.luau.
inline std::string ManualAttachStubs(const char* variant = "ok") {
    return StubChunk("manual_attach") + "\n" + StubInstalled("manual_attach_vehicles", std::string("StubManualAttach.install('") + variant + "')");
}

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same
// geometry as the Object Grab and Animated Objects tests): the rear hitch of the stand-in's tractor is straight ahead of it, 1.7 m away.
// A tractor on foot's side, within Manual Attach's reach of the player (who stands at the world origin).
constexpr const char* kTractorAhead = "_G.tractor = StubManualAttach.tractor({}); StubManualAttach.detectAll()";
// Where the ray of the hand at kHandX ends when it is on that rear hitch.
constexpr const char* kRearHitch = "-0.40 -0.20 2.00";
// Where the rays end on the tractor's Coupling Points of the rear hitch other than the hitch: its PTO stub, its hose target and its custom hose
// target. The hand that points at the hitch (kHandX, kHandY) points at these at kHandX / kHandY of kPtoHandY, kHoseHandX / kHoseHandY and
// kCustomHoseHandX / kHoseHandY (the tracking space is the world mirrored in X and Z).
constexpr const char* kRearPtoStub = "-0.40 0.20 1.70";
constexpr const char* kRearHoseCoupler = "-0.05 -0.05 1.70";
constexpr const char* kRearCustomHoseCoupler = "-0.75 -0.05 1.70";
constexpr float kPtoHandY = 0.2f, kHoseHandX = 0.05f, kCustomHoseHandX = 0.75f, kHoseHandY = -0.05f;

// A test of the Pointer Ray working hitches, seen from what Manual Attach's stand-in recorded and what the game reads from the Virtual Gamepad:
// the mod started on a player on foot with the game's empty hands, the Virtual Gamepad on, the game clock set, and Manual Attach in the game.
// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) covers.
class VirtualGamepadManualAttachTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    // The game with Manual Attach ("ok" or a variant).
    void StartWithManualAttach(const char* variant = "ok") {
        stubs_ = AnimatedObjectStubs() + ManualAttachStubs(variant);
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, StubChunk("animated_object_helpers"), "=animatedObjectHelpers"), 0);
    }

    void Lua(const std::string& chunk) { ASSERT_EQ(RunLuauChunk(L, chunk.c_str(), "=test"), 0) << chunk; }

    // How many times Manual Attach's perform path ran for an attachment ("joint", "pto", "hoses"), or in all.
    int Performs(const char* kind) { return static_cast<int>(Number(L, std::string("StubManualAttach.performCount('") + kind + "')")); }
    int Performs() { return static_cast<int>(Number(L, "#StubManualAttach.performs")); }
    int Warnings() { return CountLogLines("Manual Attach support disabled"); }
    int BlinkingWarnings() { return static_cast<int>(Number(L, "#StubManualAttach.warnings")); }
    std::string Attached(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".attacherVehicle ~= nil)"); }
    // "true" when the Implement's PTO / hoses are connected.
    std::string PtoConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".ptoConnected == true)"); }
    std::string HosesConnected(const char* implement) { return EvalLua(L, std::string("tostring(") + implement + ".hosesConnected == true)"); }

    // The game without Manual Attach at all.
    void StartWithoutManualAttach() {
        stubs_ = AnimatedObjectStubs();
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, StubChunk("animated_object_helpers"), "=animatedObjectHelpers"), 0);
    }

    // Takes note of every function of Manual Attach's class, instance, handler and attachments, and later says whether they are still as found
    // (nothing wrapped, replaced or added).
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
    // Whether the handler class's candidate update is the one Manual Attach's release came with (not wrapped by the module).
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

}  // namespace manual_attach_test
