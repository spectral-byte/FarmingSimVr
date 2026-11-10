#include "companion_mod_fixture.h"
#include "core/injector_version.h"
#include "core/logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>

// The player is warned when the Injector and the Companion Mod come from different releases (#243, #250).
// The shipped Companion Mod runs in a Luau state against the real VRBridge, the game stood in by the stubs of the
// Companion Mod test fixture. What is asserted is what the player and the logs show: the game's blinking warning,
// the lines the mod prints to log.txt and the lines the Injector writes to fs25_vr.log.

namespace {

// The version the Companion Mod is released as: modDesc.xml's <version>, the one place it is written.
std::string ReleaseVersion() {
    std::smatch version;
    const std::string modDesc = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    EXPECT_TRUE(std::regex_search(modDesc, version, std::regex("<version>([^<]+)</version>")));
    return version[1].str();
}

struct Warning {
    std::string text;
    double ms;
};

// The Injector's log (fs25_vr.log) redirected to a temp file while a test runs.
class InjectorLog {
public:
    InjectorLog() : m_path(std::filesystem::temp_directory_path() / "fs25vr_version_warning_test.log") {
        std::filesystem::remove(m_path);
        fs25vr::Logger::Instance().SetLogFile(m_path);
    }
    ~InjectorLog() {
        fs25vr::Logger::Instance().SetLogFile(fs25vr::Logger::DefaultLogPath());
        std::filesystem::remove(m_path);
    }
    std::vector<std::string> Lines() const {
        fs25vr::Logger::Instance().Flush();
        std::ifstream file(m_path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) lines.push_back(line);
        return lines;
    }
    // The lines that name every one of the needles.
    std::vector<std::string> LinesNaming(const std::vector<std::string>& needles) const {
        std::vector<std::string> found;
        for (const std::string& line : Lines()) {
            const bool namesAll = std::all_of(needles.begin(), needles.end(), [&](const std::string& n) {
                return line.find(n) != std::string::npos;
            });
            if (namesAll) found.push_back(line);
        }
        return found;
    }
private:
    std::filesystem::path m_path;
};

} // namespace

class InjectorVersionWarningTest : public CompanionModTest {
protected:
    // The game with a mission that records the blinking warnings it is asked to show.
    void SetUp() override {
        CompanionModTest::SetUp();
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            _G.shownWarnings = {}
            _G.g_currentMission = {
                showBlinkingWarning = function(self, text, ms)
                    table.insert(_G.shownWarnings, { text = text, ms = ms })
                end
            }
        )luau", "=missionStub"), 0);
    }

    void LoadMod(fs25vr::VRBridge& bridge) {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    }

    void LoadMap() {
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('mapUS')", "=loadMap"), 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    }

    std::vector<Warning> ShownWarnings() {
        std::vector<Warning> shown;
        lua_getglobal(L, "shownWarnings");
        const int count = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "text");
            const char* text = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "ms");
            const double ms = lua_tonumber(L, -1);
            lua_pop(L, 2);
            shown.push_back({ text ? text : "", ms });
        }
        lua_pop(L, 1);
        return shown;
    }

    // The warnings that are about the two versions not matching (the mod shows others, such as the HUD notice at load).
    std::vector<Warning> VersionWarnings() {
        std::vector<Warning> versionWarnings;
        for (const Warning& w : ShownWarnings()) {
            if (w.text.find("Injector") != std::string::npos) versionWarnings.push_back(w);
        }
        return versionWarnings;
    }

    // The lines the mod printed to log.txt that name every one of the needles.
    std::vector<std::string> PrintedLinesNaming(const std::vector<std::string>& needles) {
        std::vector<std::string> found;
        for (const std::string& line : s_capturedLogs) {
            const bool namesAll = std::all_of(needles.begin(), needles.end(), [&](const std::string& n) {
                return line.find(n) != std::string::npos;
            });
            if (namesAll) found.push_back(line);
        }
        return found;
    }
};

TEST_F(InjectorVersionWarningTest, MatchingVersionsShowNoWarningAndLogTheirVersions) {
    fs25vr::VRBridge bridge(nullptr, nullptr, ReleaseVersion());
    LoadMod(bridge);
    LoadMap();

    EXPECT_TRUE(VersionWarnings().empty());
    const std::string release = ReleaseVersion();
    EXPECT_EQ(PrintedLinesNaming({ "[FS25_VR]", "Companion Mod " + release, "Injector " + release }).size(), 1u);
    EXPECT_TRUE(PrintedLinesNaming({ "[Warning]", release }).empty()) << "a matching pair is not a warning in log.txt";
}

TEST_F(InjectorVersionWarningTest, DifferentVersionsShowTheBlinkingWarningNamingBothAndWhatToDo) {
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    LoadMap();

    const std::vector<Warning> shown = VersionWarnings();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].text.find(ReleaseVersion()), std::string::npos) << shown[0].text;
    EXPECT_NE(shown[0].text.find("7.8.9"), std::string::npos) << shown[0].text;
    EXPECT_NE(shown[0].text.find("same release"), std::string::npos) << shown[0].text;
    EXPECT_GE(shown[0].ms, 6000.0) << "long enough to read in the headset";

    EXPECT_EQ(PrintedLinesNaming({ "[FS25_VR] [Warning]", "Companion Mod " + ReleaseVersion(), "Injector 7.8.9" }).size(), 1u);
}

TEST_F(InjectorVersionWarningTest, AnInjectorOf1_0_0IsCalledAnOlderVersionNotQuoted) {
    // Every release up to 1.2 reported the fixed text 1.0.0, which says nothing about the release.
    fs25vr::VRBridge bridge(nullptr, nullptr, "1.0.0");
    LoadMod(bridge);
    LoadMap();

    const std::vector<Warning> shown = VersionWarnings();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(shown[0].text.find("an older version"), std::string::npos) << shown[0].text;
    EXPECT_EQ(shown[0].text.find("1.0.0"), std::string::npos) << shown[0].text;
    EXPECT_NE(shown[0].text.find(ReleaseVersion()), std::string::npos) << shown[0].text;
    EXPECT_NE(shown[0].text.find("same release"), std::string::npos) << shown[0].text;
    EXPECT_EQ(PrintedLinesNaming({ "[FS25_VR] [Warning]" , "Injector" }).size(), 1u) << "log.txt has the warning too";
}

TEST_F(InjectorVersionWarningTest, ThePassThroughModePlayerSeesTheWarningToo) {
    // No headset session: the Injector forwards the game's calls and VRBridge says it is not ready.
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(assert(VRBridge.isReady() == false, "no VR session runs"))luau", "=passThrough"), 0);
    LoadMap();

    EXPECT_EQ(VersionWarnings().size(), 1u);
}

TEST_F(InjectorVersionWarningTest, TheWarningIsShownOncePerMapLoad) {
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    LoadMap();
    for (int i = 0; i < 30; ++i) NextFrame();
    EXPECT_EQ(VersionWarnings().size(), 1u) << "frames after the map load do not repeat it";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(VersionWarnings().size(), 1u) << "unloading the map does not show it";

    LoadMap();
    for (int i = 0; i < 30; ++i) NextFrame();
    EXPECT_EQ(VersionWarnings().size(), 2u) << "the next map load shows it again";
}

TEST_F(InjectorVersionWarningTest, AMismatchChangesNothingElseVrKeepsRunning) {
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    // An Injector without some function the mod would call: the mod still checks each before calling it.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRBridge.setFovOverscan = nil
        VRBridge.setDesktopMirror = nil
        VRBridge.setMenuDistance = nil
    )luau", "=removeFunctions"), 0);
    LoadMap();
    for (int i = 0; i < 5; ++i) NextFrame();

    EXPECT_EQ(VersionWarnings().size(), 1u);
    EXPECT_EQ(EvalLua(L, "VRBridge.isCompanionActive()"), "true") << "the Injector still sees a Companion Mod that handshook";
    EXPECT_EQ(PrintedLinesNaming({ "Handshake successful" }).size(), 1u);
    EXPECT_TRUE(PrintedLinesNaming({ "Handshake failed" }).empty());
}

TEST_F(InjectorVersionWarningTest, TheModPassesItsVersionToTheHandshake) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    LoadMod(bridge);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local realHandshake = VRBridge.handshake
        _G.handshakeArgs = {}
        VRBridge.handshake = function(...)
            table.insert(_G.handshakeArgs, { ... })
            return realHandshake(...)
        end
    )luau", "=spyOnHandshake"), 0);
    LoadMap();

    ASSERT_EQ(EvalLua(L, "#handshakeArgs"), "1");
    EXPECT_EQ(EvalLua(L, "handshakeArgs[1][2]"), ReleaseVersion());
    EXPECT_EQ(EvalLua(L, "handshakeArgs[1][1]"), "nil") << "the first argument stays the optional rendering mode";
}

TEST_F(InjectorVersionWarningTest, TheInjectorLogsBothVersionsAtTheHandshake) {
    InjectorLog log;
    fs25vr::VRBridge bridge(nullptr, nullptr, ReleaseVersion());
    LoadMod(bridge);
    LoadMap();

    const std::string release = ReleaseVersion();
    EXPECT_EQ(log.LinesNaming({ "Injector " + release, "Companion Mod " + release }).size(), 1u);
    EXPECT_TRUE(log.LinesNaming({ "[WARN]", "Companion Mod" }).empty()) << "a matching pair is not a warning";
}

TEST_F(InjectorVersionWarningTest, TheInjectorLogsAWarningWhenTheModsVersionDiffers) {
    InjectorLog log;
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    LoadMap();

    EXPECT_EQ(log.LinesNaming({ "[WARN]", "Injector 7.8.9", "Companion Mod " + ReleaseVersion() }).size(), 1u);
}

TEST_F(InjectorVersionWarningTest, AHandshakeThatPassesNoVersionCompletesAndTheInjectorLogsThatNoneWasReported) {
    // A Companion Mod of 1.2 or earlier does not pass a version.
    InjectorLog log;
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local result = VRBridge.handshake()
        assert(result.success == true, "the handshake completes")
        assert(VRBridge.isCompanionActive() == true)
    )luau", "=oldModHandshake"), 0);

    EXPECT_EQ(log.LinesNaming({ "no version", "Injector 7.8.9" }).size(), 1u);
}

TEST_F(InjectorVersionWarningTest, TheReadmeTroubleshootingExplainsTheWarning) {
    const std::string readme = ReadFile("README.md");
    const size_t troubleshooting = readme.find("## Troubleshooting");
    ASSERT_NE(troubleshooting, std::string::npos);
    const size_t entry = readme.find("### The Injector and the Companion Mod are from different versions", troubleshooting);
    ASSERT_NE(entry, std::string::npos) << "a troubleshooting entry for the warning";
    const size_t entryEnd = std::min(readme.find("\n### ", entry + 1), readme.find("\n## ", entry + 1));
    const std::string text = readme.substr(entry, entryEnd - entry);

    // It names what the player sees on the screen and what to do about it.
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    LoadMod(bridge);
    LoadMap();
    const std::vector<Warning> shown = VersionWarnings();
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_NE(text.find("Install both files from the same release"), std::string::npos);
    EXPECT_NE(shown[0].text.find("Install both files from the same release"), std::string::npos)
        << "the README quotes the sentence the warning ends with";
    EXPECT_NE(text.find("version.dll"), std::string::npos) << "it names the Injector's file";
    EXPECT_NE(text.find("FS25_VRCompanion.zip"), std::string::npos) << "and the Companion Mod's file";
    EXPECT_NE(text.find("an older version"), std::string::npos) << "and what the warning says of a 1.2 or earlier Injector";
}

TEST_F(InjectorVersionWarningTest, TheModsVersionConstantIsTheVersionInModDesc) {
    // The Companion Mod holds its version as a constant, and a release bumps modDesc.xml: this fails when they disagree.
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion.VERSION"), ReleaseVersion());
}
