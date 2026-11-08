// Menu Distance: `vrMenuDistance [1.0-5.0]` sets how far in front of the head the menu quad (menus and 2D Floating Window
// Mode) is placed. The setting reaches the Injector through VRBridge, is saved in settings.xml and is sent again on the next
// map load. These tests run the Companion Mod against a real VRBridge on the Fake Tracked Headset and look only at what leaves
// the system: where the menu quad is placed, the VRBridge getter, the console replies and the settings store.
#include "companion_mod_fixture.h"
#include "core/companion_settings.h"
#include "core/injector_runtime.h"
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

// A settings.xml in the temp folder, removed again when the test ends.
class TempSettingsFile {
public:
    explicit TempSettingsFile(const std::string& content)
        : m_path(std::filesystem::temp_directory_path() / "fs25vr_menu_distance_settings_test.xml") {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out << content;
    }
    ~TempSettingsFile() { std::filesystem::remove(m_path); }
    const std::filesystem::path& Path() const { return m_path; }
private:
    std::filesystem::path m_path;
};

// settings.xml as the engine writes it: UTF-8 with a BOM, CRLF line ends, floats with six decimals.
std::string EngineSettingsXml(const std::string& menuDistanceElement) {
    return "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\" ?>\r\n"
           "<vrSettings>\r\n"
           "    <hudSafeAreaHorizontal>0.300000</hudSafeAreaHorizontal>\r\n"
           "    <hudConvergenceDepth>1.800000</hudConvergenceDepth>\r\n"
           "    <fovOverscan>0.000000</fovOverscan>\r\n" +
           menuDistanceElement +
           "    <snapTurnAngle>90.000000</snapTurnAngle>\r\n"
           "</vrSettings>\r\n";
}

// The head at eye height looking down -Z, no recenter: the quad goes straight ahead of it.
void PlaceHead(fs25vr::FakeTrackedHeadset& headset) {
    fs25vr::HeadPose head;
    head.position = fs25vr::Vector3(0.0f, 1.7f, 0.0f);
    head.orientation = fs25vr::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    head.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    head.isValid = true;
    headset.SetHeadPose(head);
    headset.manager->PollHeadPose();
}

// How far in front of the head, along -Z, the menu quad sits
float QuadDistanceAhead(fs25vr::FakeTrackedHeadset& headset) {
    return -headset.manager->ComputeMenuQuadPose().position.z;
}

} // namespace

class MenuDistanceCommandTest : public CompanionModTest {
protected:
    // Engine functions a map load needs, recording the console commands registered and removed, over a settings file that
    // is a store the mocks write to and read from (a value saved in one map load is what the next one finds).
    static constexpr const char* kMapLoadMocks = R"luau(
        _G.registeredCommands = {}
        _G.removedCommands = {}
        _G.addConsoleCommand = function(cmd, desc, funcName, target)
            _G.registeredCommands[cmd] = { desc = desc, funcName = funcName, target = target }
        end
        _G.removeConsoleCommand = function(cmd) _G.removedCommands[cmd] = true end
        _G.createFolder = function() return true end
        _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
        _G.settingsStore = { floats = {}, bools = {}, strings = {}, ints = {} }
        _G.settingsSaves = 0
        _G.createXMLFile = function() return 1 end
        _G.loadXMLFile = function() return 2 end
        _G.setXMLFloat = function(xml, key, value) _G.settingsStore.floats[key] = value end
        _G.setXMLBool = function(xml, key, value) _G.settingsStore.bools[key] = value end
        _G.setXMLString = function(xml, key, value) _G.settingsStore.strings[key] = value end
        _G.setXMLInt = function(xml, key, value) _G.settingsStore.ints[key] = value end
        _G.getXMLFloat = function(xml, key) return _G.settingsStore.floats[key] end
        _G.getXMLBool = function(xml, key) return _G.settingsStore.bools[key] end
        _G.getXMLString = function(xml, key) return _G.settingsStore.strings[key] end
        _G.getXMLInt = function(xml, key) return _G.settingsStore.ints[key] end
        _G.saveXMLFile = function() _G.settingsSaves = _G.settingsSaves + 1; return true end
        _G.delete = function() end
    )luau";

    void LoadModAndMap(fs25vr::VRBridge& bridge) {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kMapLoadMocks, "=mapLoadMocks"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('testMap')", "=loadMap"), 0);
    }
};

TEST_F(MenuDistanceCommandTest, TheCommandIsRegisteredOnMapLoadAndRemovedOnMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local command = _G.registeredCommands["vrMenuDistance"]
        assert(command ~= nil, "vrMenuDistance must be registered on map load")
        assert(command.funcName == "consoleCommandSetMenuDistance", "registered callback: " .. tostring(command.funcName))
        assert(command.target == VRCompanion)
        assert(string.find(command.desc, "vrMenuDistance [1.0-5.0]", 1, true) ~= nil, "help names the usage: " .. command.desc)
        VRCompanion:deleteMap()
        assert(_G.removedCommands["vrMenuDistance"] == true, "deleteMap must unregister vrMenuDistance")
    )luau", "=registration"), 0);
}

TEST_F(MenuDistanceCommandTest, WithoutAnArgumentItReportsTheDistanceAndTheUsage) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local report = VRCompanion:consoleCommandSetMenuDistance()
        assert(string.find(report, "Menu distance is 1.5 m", 1, true) ~= nil, report)
        assert(string.find(report, "Usage: vrMenuDistance [1.0-5.0] (metres, saved, 1.5 by default)", 1, true) ~= nil, report)
        VRCompanion:consoleCommandSetMenuDistance("3")
        report = VRCompanion:consoleCommandSetMenuDistance("")
        assert(string.find(report, "Menu distance is 3 m", 1, true) ~= nil, report)
    )luau", "=report"), 0);
}

TEST_F(MenuDistanceCommandTest, ANumberFromOneToFiveMovesTheShownMenuAtOnceAndPlacesTheNextMenuThatFar) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    PlaceHead(headset);

    // A menu opens at the default distance
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuVisible(true)", "=openMenu"), 0);
    EXPECT_NEAR(QuadDistanceAhead(headset), 1.5f, 0.001f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local reply = VRCompanion:consoleCommandSetMenuDistance("3")
        assert(string.find(reply, "Menu distance set to: 3 m (Saved)", 1, true) ~= nil, reply)
        assert(VRBridge.getMenuDistance() == 3, "the value reaches VRBridge")
    )luau", "=set"), 0);
    EXPECT_NEAR(QuadDistanceAhead(headset), 3.0f, 0.001f) << "the menu showing moves back at once";

    // The next menu is placed at the new distance too
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuVisible(false); VRBridge.setMenuVisible(true)", "=reopenMenu"), 0);
    EXPECT_NEAR(QuadDistanceAhead(headset), 3.0f, 0.001f);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:consoleCommandSetMenuDistance("2.25")
        assert(VRBridge.getMenuDistance() == 2.25, "decimals are allowed")
        VRCompanion:consoleCommandSetMenuDistance("5")
        assert(VRBridge.getMenuDistance() == 5, "5 is the largest value")
        VRCompanion:consoleCommandSetMenuDistance("1")
        assert(VRBridge.getMenuDistance() == 1, "1 is the smallest value")
    )luau", "=range"), 0);
    EXPECT_NEAR(QuadDistanceAhead(headset), 1.0f, 0.001f);
}

TEST_F(MenuDistanceCommandTest, AnythingElseReturnsTheUsageAndChangesNothing) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:consoleCommandSetMenuDistance("2.5")
        local savesBefore = _G.settingsSaves
        for _, bad in ipairs({ "0.9", "5.1", "0", "-2", "far", "2.5m", "nan", "inf", "-inf", "1e3", "on", " " }) do
            local reply = VRCompanion:consoleCommandSetMenuDistance(bad)
            assert(string.find(reply, "Usage: vrMenuDistance [1.0-5.0] (metres, saved, 1.5 by default)", 1, true) ~= nil,
                "'" .. bad .. "' returns the usage: " .. tostring(reply))
            assert(VRBridge.getMenuDistance() == 2.5, "'" .. bad .. "' must not change the distance")
            assert(VRCompanion.menuDistance == 2.5, "'" .. bad .. "' must not change the setting")
        end
        VRCompanion:consoleCommandSetMenuDistance()
        assert(_G.settingsSaves == savesBefore, "usage and status must not write the file")
    )luau", "=refusals"), 0);
    EXPECT_FLOAT_EQ(headset.manager->GetMenuQuadDistance(), 2.5f);
}

TEST_F(MenuDistanceCommandTest, LeavingToTheMainMenuKeepsTheDistanceForTheMainMenu) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    PlaceHead(headset);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local savesBefore = _G.settingsSaves
        VRCompanion:consoleCommandSetMenuDistance("3.5")
        assert(_G.settingsSaves == savesBefore + 1, "the command writes settings.xml")
        assert(_G.settingsStore.floats["vrSettings.menuDistance"] == 3.5, "saved under vrSettings.menuDistance")

        VRCompanion:deleteMap()
        assert(VRBridge.getMenuDistance() == 3.5, "a map unload leaves the distance")
        VRBridge.setMenuVisible(true)
    )luau", "=saveUnloadAndShowMainMenu"), 0);
    EXPECT_NEAR(QuadDistanceAhead(headset), 3.5f, 0.001f) << "the main menu is shown at the distance";
}

TEST_F(MenuDistanceCommandTest, TheNextMapLoadSendsWhatSettingsXmlHoldsAfterTheHandshake) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:consoleCommandSetMenuDistance("3.5")
        VRCompanion:deleteMap()
        -- Start the game again (a fresh Companion Mod), with settings.xml changed by hand in between
        VRCompanion = nil
        _G.settingsStore.floats["vrSettings.menuDistance"] = 4
    )luau", "=saveAndUnload"), 0);

    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(VRCompanion.menuDistance == 1.5, "a fresh Companion Mod starts from the default")
        local calls = {}
        local realHandshake = VRBridge.handshake
        local realSet = VRBridge.setMenuDistance
        VRBridge.handshake = function(...) table.insert(calls, "handshake"); return realHandshake(...) end
        VRBridge.setMenuDistance = function(metres) table.insert(calls, "distance:" .. tostring(metres)); return realSet(metres) end
        VRCompanion:loadMap("otherMap")
        VRBridge.handshake = realHandshake
        VRBridge.setMenuDistance = realSet
        assert(VRCompanion.menuDistance == 4, "the saved value is read with the rest of settings.xml")
        assert(VRBridge.getMenuDistance() == 4, "and reaches VRBridge")
        local handshakeAt, distanceAt = nil, nil
        for i, c in ipairs(calls) do
            if c == "handshake" and handshakeAt == nil then handshakeAt = i end
            if c == "distance:4" then distanceAt = i end
        end
        assert(handshakeAt ~= nil and distanceAt ~= nil and distanceAt > handshakeAt,
            "the value is sent after the handshake: " .. table.concat(calls, ","))
    )luau", "=reload"), 0);
    EXPECT_FLOAT_EQ(headset.manager->GetMenuQuadDistance(), 4.0f);
}

TEST_F(MenuDistanceCommandTest, AMissingKeyKeepsTheDefaultAndASavedValueOutOfRangeIsNotUsed) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadModAndMap(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        assert(_G.settingsStore.floats["vrSettings.menuDistance"] == nil, "nothing saved yet")
        assert(VRCompanion.menuDistance == 1.5, "a missing key keeps 1.5")
        assert(VRBridge.getMenuDistance() == 1.5)
        VRCompanion:deleteMap()

        -- A settings file edited by hand to something the command would refuse
        _G.settingsStore.floats["vrSettings.menuDistance"] = 30
        VRCompanion:loadMap("testMap")
        assert(VRCompanion.menuDistance == 1.5, "30 is not used: " .. VRCompanion.menuDistance)
        assert(VRBridge.getMenuDistance() == 1.5)
    )luau", "=missingKey"), 0);
}

// The Injector reads the saved Menu Distance from the Companion Mod's settings.xml when the game starts, so the main menu,
// shown before any savegame (and so before the Companion Mod) has loaded, is placed at it.

TEST(CompanionSettingsTest, ReadsTheNumberOfAnElementAsTheEngineWritesIt) {
    TempSettingsFile file(EngineSettingsXml("    <menuDistance>2.500000</menuDistance>\r\n"));
    EXPECT_EQ(fs25vr::ReadCompanionSettingFloat(file.Path(), "menuDistance"), 2.5f);
    EXPECT_EQ(fs25vr::ReadCompanionSettingFloat(file.Path(), "hudConvergenceDepth"), 1.8f);
    EXPECT_EQ(fs25vr::ReadCompanionSettingFloat(file.Path(), "fovOverscan"), 0.0f);
}

TEST(CompanionSettingsTest, AnythingButAFiniteNumberInTheElementIsNoValue) {
    {
        TempSettingsFile file(EngineSettingsXml(""));
        EXPECT_FALSE(fs25vr::ReadCompanionSettingFloat(file.Path(), "menuDistance").has_value()) << "no element";
    }
    for (const char* text : { "", "  ", "far", "2.5 m", "nan", "inf", "2,5" }) {
        TempSettingsFile file(EngineSettingsXml(std::string("    <menuDistance>") + text + "</menuDistance>\r\n"));
        EXPECT_FALSE(fs25vr::ReadCompanionSettingFloat(file.Path(), "menuDistance").has_value()) << "'" << text << "'";
    }
    {
        TempSettingsFile file(EngineSettingsXml("    <menuDistance>3.000000\r\n"));
        EXPECT_FALSE(fs25vr::ReadCompanionSettingFloat(file.Path(), "menuDistance").has_value()) << "an element left open";
    }
    EXPECT_FALSE(fs25vr::ReadCompanionSettingFloat(std::filesystem::temp_directory_path() / "fs25vr_no_such_settings.xml",
                                                   "menuDistance").has_value()) << "no file";
}

TEST(CompanionSettingsTest, TheDefaultFileIsTheCompanionModsSettingsUnderTheGamesMyGamesFolder) {
    const std::filesystem::path path = fs25vr::DefaultCompanionSettingsPath();
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.filename(), "settings.xml");
    EXPECT_EQ(path.parent_path().filename(), "FS25_VRCompanion");
    EXPECT_EQ(path.parent_path().parent_path().filename(), "modSettings");
    EXPECT_EQ(path.parent_path().parent_path().parent_path().filename(), "FarmingSimulator2025");
    EXPECT_EQ(path.parent_path().parent_path().parent_path().parent_path().filename(), "My Games");
}

namespace {

// An Injector started over `settingsFile` on the Fake Tracked Headset, then a menu shown with the head placed:
// how far ahead the menu is.
float MainMenuDistanceAtLaunch(const std::filesystem::path& settingsFile, float* bridgeDistance = nullptr) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::InjectorRuntime::Dependencies deps;
    deps.openXrManager = headset.manager;
    deps.companionSettingsFile = settingsFile;
    fs25vr::InjectorRuntime runtime(std::move(deps));
    PlaceHead(headset);
    runtime.GetVrBridge().SetMenuVisible(true);
    if (bridgeDistance != nullptr) *bridgeDistance = runtime.GetVrBridge().GetMenuDistance();
    return QuadDistanceAhead(headset);
}

} // namespace

TEST(MenuDistanceAtLaunchTest, TheMainMenuIsShownAtTheSavedDistanceBeforeTheCompanionModRuns) {
    TempSettingsFile file(EngineSettingsXml("    <menuDistance>3.000000</menuDistance>\r\n"));
    float bridgeDistance = 0.0f;
    EXPECT_NEAR(MainMenuDistanceAtLaunch(file.Path(), &bridgeDistance), 3.0f, 0.001f);
    EXPECT_FLOAT_EQ(bridgeDistance, 3.0f) << "VRBridge reports the saved distance too";
}

TEST(MenuDistanceAtLaunchTest, WithoutAUsableSavedDistanceTheMainMenuIsAtTheDefault) {
    {
        TempSettingsFile file(EngineSettingsXml(""));
        EXPECT_NEAR(MainMenuDistanceAtLaunch(file.Path()), 1.5f, 0.001f) << "nothing saved yet";
    }
    {
        TempSettingsFile file(EngineSettingsXml("    <menuDistance>30.000000</menuDistance>\r\n"));
        EXPECT_NEAR(MainMenuDistanceAtLaunch(file.Path()), 1.5f, 0.001f) << "a value the command would refuse is not used";
    }
    {
        TempSettingsFile file(EngineSettingsXml("    <menuDistance>0.500000</menuDistance>\r\n"));
        EXPECT_NEAR(MainMenuDistanceAtLaunch(file.Path()), 1.5f, 0.001f) << "nor one below the range";
    }
    EXPECT_NEAR(MainMenuDistanceAtLaunch(std::filesystem::temp_directory_path() / "fs25vr_no_such_settings.xml"), 1.5f, 0.001f)
        << "no settings file";
    EXPECT_NEAR(MainMenuDistanceAtLaunch({}), 1.5f, 0.001f) << "no file given: nothing is read";
}

TEST_F(MenuDistanceCommandTest, TheCommandIsHarmlessWithoutTheNativeBridgeFunctions) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRBridge = { version = "1.0.0", handshake = function() return true end }
        local reply = VRCompanion:consoleCommandSetMenuDistance("2")
        assert(type(reply) == "string")
        assert(type(VRCompanion:consoleCommandSetMenuDistance()) == "string")
    )luau", "=withoutBridgeFunctions"), 0);
}
