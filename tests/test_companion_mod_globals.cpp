// The globals the Companion Mod creates are pinned (spec #280, ticket #288), so a missing `local` or a mistyped name that
// leaks a global fails a test. The shipped mod runs in a Luau state with the engine stubs: it is loaded in modDesc.xml
// order, a map is loaded, the mod updates on foot (holding a Hand Tool) and in a cab, and the map is unloaded. Every
// global created along the way is attributed to the function that created it, and only those created by the mod's own
// files count: the stubs and this fixture create many, which the game would have defined.

#include <gtest/gtest-spi.h>
#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <map>
#include <set>

namespace {

using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

// The globals the Companion Mod creates on purpose. Add one here only when the mod means to create it; a global that
// leaked by accident is fixed in the mod.
const std::set<std::string> kPinnedModGlobals = {
    "VRCompanion",  // the mod itself: every file adds to it, and addModEventListener(VRCompanion) hands it to the game
};

// Records, for every global created from now on, the source of the function that created it (debug.info(2, "s"), with
// the chunk name's "=" or "@" removed): __newindex on _G only runs for a name that is not there yet.
constexpr const char* kTrackGlobalCreation = R"luau(
    local created = {}
    _G.__globalCreators = created
    setmetatable(_G, {
        __newindex = function(t, name, value)
            if created[name] == nil then
                local source = debug.info(2, "s") or "?"
                created[name] = (string.gsub(source, "^[=@]", ""))
            end
            rawset(t, name, value)
        end,
    })
)luau";

// What the game defines before any mod runs and the Companion Mod assigns: the HUD anchors (ADR-0010) and the console.
constexpr const char* kEngineGlobals = R"luau(
    g_hudAnchorLeft, g_hudAnchorRight, g_hudAnchorBottom, g_hudAnchorTop = 0, 1, 0, 1
    _G.addConsoleCommand = function() end
    _G.removeConsoleCommand = function() end
)luau";

// The globals created by the Companion Mod's own files, by name, with the file that created each.
std::map<std::string, std::string> GlobalsTheModCreated(lua_State* L) {
    std::set<std::string> modFiles;
    for (const auto& rel : CompanionModLoadOrder()) {
        modFiles.insert(std::filesystem::path(rel).filename().string());
    }
    std::map<std::string, std::string> result;
    lua_getglobal(L, "__globalCreators");
    if (!lua_istable(L, -1)) {
        ADD_FAILURE() << "global creation was not tracked";
        lua_pop(L, 1);
        return result;
    }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        const char* name = lua_tostring(L, -2);
        const char* source = lua_tostring(L, -1);
        if (name && source && modFiles.count(source)) result[name] = source;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return result;
}

// Fails naming every global the mod created that is not pinned, and every pinned global it no longer creates.
void ExpectOnlyPinnedGlobals(const std::map<std::string, std::string>& created, const std::set<std::string>& pinned) {
    std::string unpinned;
    for (const auto& [name, file] : created) {
        if (!pinned.count(name)) unpinned += "\n  " + name + " (created in " + file + ")";
    }
    std::string missing;
    for (const auto& name : pinned) {
        if (!created.count(name)) missing += "\n  " + name;
    }
    EXPECT_TRUE(unpinned.empty()) << "the Companion Mod created globals that are not pinned; make each local, or pin it if "
                                     "the mod means to create it:" << unpinned;
    EXPECT_TRUE(missing.empty()) << "pinned globals the Companion Mod no longer creates; remove them from the list:"
                                 << missing;
}

// Self-contained so that EXPECT_NONFATAL_FAILURE can run them.
void CompareALeakedGlobal() {
    ExpectOnlyPinnedGlobals({{"VRCompanion", "VRCompanion.lua"}, {"leakedName", "VRCompanionHud.lua"}}, {"VRCompanion"});
}
void CompareAPinnedGlobalNoLongerCreated() {
    ExpectOnlyPinnedGlobals({{"VRCompanion", "VRCompanion.lua"}}, {"VRCompanion", "goneName"});
}

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// What the game does each frame before the Companion Mod's update.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
        StubHandTool.tick(16.667)
    )luau", "=preFrame"), 0);
}

class CompanionModGlobalsTest : public CompanionModTest {
protected:
    // The mod's whole life: loaded with the stubs in place, a map loaded, updates on foot holding the lance and then in a
    // cab with the Motion Controllers moving, and the map unloaded.
    void RunTheModsLife() {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(RunLuauChunk(L, kTrackGlobalCreation, "=trackGlobals"), 0);
        ASSERT_EQ(RunLuauChunk(L, kEngineGlobals, "=engineGlobals"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("model_loading"), "=modelLoading"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("interactive_control"), "=interactiveControl"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("hand_tool"), "=handTool"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.install('ok')", "=installHandTool"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        SetGameTime(10000);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);

        ASSERT_EQ(RunLuauChunk(L, R"luau(
            local scene = StubHandTool.onFoot({})
            local lance = StubHandTool.lance(scene.player)
            StubHandTool.hold(lance)
        )luau", "=onFoot"), 0);
        for (int frame = 0; frame < 10; ++frame) {
            headset.SetHandPose(frame % 2, HandAt(0.2f + 0.01f * frame, 0.0f, -0.3f));
            PreFrame(L);
            NextFrame(&headset);
        }

        ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=cab"), 0);
        for (int frame = 0; frame < 10; ++frame) {
            headset.SetHandPose(frame % 2, HandAt(0.2f + 0.01f * frame, 0.0f, -0.3f));
            PreFrame(L);
            NextFrame(&headset);
        }

        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    }
};

}  // namespace

TEST_F(CompanionModGlobalsTest, TheModCreatesOnlyThePinnedGlobals) {
    ASSERT_NO_FATAL_FAILURE(RunTheModsLife());
    ExpectOnlyPinnedGlobals(GlobalsTheModCreated(L), kPinnedModGlobals);
}

TEST_F(CompanionModGlobalsTest, GlobalsTheStubsCreateWhileTheModRunsAreNotCounted) {
    ASSERT_NO_FATAL_FAILURE(RunTheModsLife());
    // StubScene.cab sets g_currentMission.controlledVehicle and the active camera; StubHandTool.onFoot sets g_localPlayer.
    EXPECT_EQ(EvalLua(L, "__globalCreators.g_localPlayer"), "handTool");
    EXPECT_EQ(GlobalsTheModCreated(L).count("g_localPlayer"), 0u);
}

TEST(CompanionModGlobalsCheck, AGlobalThatIsNotPinnedFailsNamingIt) {
    EXPECT_NONFATAL_FAILURE(CompareALeakedGlobal(), "leakedName (created in VRCompanionHud.lua)");
}

TEST(CompanionModGlobalsCheck, APinnedGlobalTheModNoLongerCreatesFailsNamingIt) {
    EXPECT_NONFATAL_FAILURE(CompareAPinnedGlobalNoLongerCreated(), "goneName");
}
