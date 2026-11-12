#pragma once

// Engine and mod stubs for Companion Mod tests. Each stub is a Luau file under tests/stubs/, read from disk by name
// every time a test asks for it, so an edited stub takes effect on the next test run without a rebuild:
//
//   ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);   // runs tests/stubs/scene_graph.luau
//
// Each file starts with what it models and the functions it offers. The shared ones:
//
//   scene_graph            a small scene graph (nodes, parents, local and world transforms) with the engine's node
//                          functions, and StubScene.cab / StubScene.onFoot building a camera chain the real Companion Mod drives
//   model_loading          the engine's model loading and node functions the Virtual Hands use (StubModel), on top of
//                          the scene graph: a loadable hand model with finger joints, clones, visibility, scale, world
//                          transforms, joint rotations, shadow flags, and a model that is missing, failing or lacking joints
//   interactive_control    a stand-in for the Interactive Control mod's Click Point class and manager (StubIC), with a
//                          mismatched variant
//   input_binding          a stand-in for g_inputBinding and Binding (StubInput), shaped after the finding of spike #99:
//                          the actions and their bindings per device, addBinding, saveToXMLFile, with variants that lack
//                          the API or raise
//   steering_vehicle       a cab vehicle with a Steering Wheel and the game's analog steering (StubSteer): the
//                          Drivable.setSteeringInput that records every call, a Steering Wheel node and the per-frame
//                          tick that moves the steering toward the input and turns the node (ADR-0027)
//   hand_tool              a player on foot who holds Hand Tools (StubHandTool): the game's HandTool, Player and
//                          PlayerTargeter, the pressure washer lance and another mod's tool (which the Companion Mod
//                          does not support), with attachToolToCamera / getLookRay install variants and extension
//                          points for the flashlight, the chainsaw, the spray can and the look ray (ADR-0029)
//   hand_tool_flashlight   the flashlight for StubHandTool (run after hand_tool and model_loading)
//   hand_tool_chainsaw     the chainsaw for StubHandTool (ADR-0029)
//   hand_tool_spray_can    the spray can for StubHandTool (ADR-0030)
//   hand_tool_horse_brush  the horse brush for StubHandTool (ADR-0030)
//   hand_tool_fishing_rod  the Highlands Fishing DLC's rod for StubHandTool (ADR-0031)
//
// They model only the names and shapes the Companion Mod depends on (ADR-0026, ADR-0027). Nothing here is
// copied from Interactive Control or from the game.

#include <gtest/gtest.h>
#include "luau_test_runner.h"
#include <fstream>
#include <sstream>
#include <string>

// The repo-relative path of the stub called `name`: tests/stubs/<name>.luau.
inline std::string StubPath(const std::string& name) { return "tests/stubs/" + name + ".luau"; }

// The Luau text of tests/stubs/<name>.luau, found from the working directories the tests support (ResolveProjectPath).
// A stub that cannot be found or read fails the test with a message naming the file, and its chunk raises when run.
inline std::string StubChunk(const std::string& name) {
    const std::string path = StubPath(name);
    const auto resolved = ResolveProjectPath(path);
    std::ifstream file;
    if (!resolved.empty()) file.open(resolved);
    if (!file.is_open()) {
        ADD_FAILURE() << "engine stub not found: " << path;
        return "error('engine stub not found: " + path + "')\n";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
