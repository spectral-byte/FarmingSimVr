#include <gtest/gtest-spi.h>
#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <map>
#include <set>

namespace {

using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

const std::set<std::string> kPinnedModGlobals = {
    "VRCompanion",
};

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

constexpr const char* kEngineGlobals = R"luau(
    g_hudAnchorLeft, g_hudAnchorRight, g_hudAnchorBottom, g_hudAnchorTop = 0, 1, 0, 1
    _G.addConsoleCommand = function() end
    _G.removeConsoleCommand = function() end
)luau";

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

void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
        StubHandTool.tick(16.667)
    )luau", "=preFrame"), 0);
}

class CompanionModGlobalsTest : public CompanionModTest {
protected:
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

}

TEST_F(CompanionModGlobalsTest, TheModCreatesOnlyThePinnedGlobals) {
    ASSERT_NO_FATAL_FAILURE(RunTheModsLife());
    ExpectOnlyPinnedGlobals(GlobalsTheModCreated(L), kPinnedModGlobals);
}

TEST_F(CompanionModGlobalsTest, GlobalsTheStubsCreateWhileTheModRunsAreNotCounted) {
    ASSERT_NO_FATAL_FAILURE(RunTheModsLife());
    EXPECT_EQ(EvalLua(L, "__globalCreators.g_localPlayer"), "handTool");
    EXPECT_EQ(GlobalsTheModCreated(L).count("g_localPlayer"), 0u);
}

TEST(CompanionModGlobalsCheck, AGlobalThatIsNotPinnedFailsNamingIt) {
    EXPECT_NONFATAL_FAILURE(CompareALeakedGlobal(), "leakedName (created in VRCompanionHud.lua)");
}

TEST(CompanionModGlobalsCheck, APinnedGlobalTheModNoLongerCreatesFailsNamingIt) {
    EXPECT_NONFATAL_FAILURE(CompareAPinnedGlobalNoLongerCreated(), "goneName");
}
