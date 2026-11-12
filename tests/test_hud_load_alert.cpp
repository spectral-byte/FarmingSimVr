// The map-load HUD alert (ticket #283, spec #280): with the alert on, a map load shows a blinking warning that names the HUD state
// the HUD applied (Hidden, Streamlined or Full) and no key, in the game's language (English or German), English where the game
// has no text for the key. Seen from outside: the shipped Companion Mod is loaded in modDesc order, the settings file is a table
// of saved values, the game's texts are a stub g_i18n, and the test asserts what the game was asked to show.

#include "companion_mod_fixture.h"
#include <string>
#include <tuple>
#include <vector>

namespace {

// The settings file as a table of saved values (`saved`), the mission recording the blinking warnings it is asked to show.
const char* kEngineStubs = R"luau(
    _G.saved = {}
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.saved[key] = value end
    _G.setXMLBool = function(file, key, value) _G.saved[key] = value end
    _G.setXMLString = function(file, key, value) _G.saved[key] = value end
    _G.saveXMLFile = function() return true end
    _G.loadXMLFile = function() return 2 end
    _G.delete = function() end
    _G.getXMLFloat = function(file, key) local v = _G.saved[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.saved[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.saved[key]; return type(v) == "string" and v or nil end
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.shownWarnings = {}
    _G.g_currentMission = {
        showBlinkingWarning = function(self, text, ms) table.insert(_G.shownWarnings, text) end
    }
)luau";

// The game's texts in one language, as the game's I18N has them: hasText, and getText reading "Missing ..." for a missing key.
std::string I18nStub(const std::string& texts) {
    return "local texts = { " + texts + R"luau( }
        _G.g_i18n = {
            hasText = function(self, name) return texts[name] ~= nil end,
            getText = function(self, name) return texts[name] or ("Missing '" .. tostring(name) .. "' in l10n") end,
        }
    )luau";
}

const char* kEnglishTexts = R"(
    vrHud_loadAlert_hidden = "VR HUD: Hidden",
    vrHud_loadAlert_streamlined = "VR HUD: Streamlined",
    vrHud_loadAlert_full = "VR HUD: Full",
)";

const char* kGermanTexts = R"(
    vrHud_loadAlert_hidden = "VR-HUD: Ausgeblendet",
    vrHud_loadAlert_streamlined = "VR-HUD: Reduziert",
    vrHud_loadAlert_full = "VR-HUD: Vollständig",
)";

class HudLoadAlertTest : public CompanionModTest {
protected:
    void SetUp() override {
        CompanionModTest::SetUp();
        ASSERT_TRUE(bridge_.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kEngineStubs, "=hudLoadAlertStubs"), 0);
    }

    void UseGameTexts(const std::string& texts) {
        ASSERT_EQ(RunLuauChunk(L, I18nStub(texts).c_str(), "=i18n"), 0);
    }

    // A map load with this HUD state saved and the alert switched on in the settings file.
    void LoadMapWithSavedHudState(const std::string& state) {
        const std::string chunk = "saved['vrSettings.showHudLoadNotification'] = true\n"
                                  "saved['vrSettings.hudState'] = '" + state + "'\n"
                                  "VRCompanion:loadMap('mapUS')";
        ASSERT_EQ(RunLuauChunk(L, chunk.c_str(), "=loadMap"), 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    }

    void UnloadMap() { ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap(); shownWarnings = {}", "=deleteMap"), 0); }

    std::vector<std::string> ShownWarnings() {
        std::vector<std::string> shown;
        lua_getglobal(L, "shownWarnings");
        const int count = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            shown.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return shown;
    }

    std::string Shown() {
        std::string all;
        for (const std::string& w : ShownWarnings()) all += "[" + w + "]";
        return all;
    }

    fs25vr::VRBridge bridge_{nullptr, nullptr};
};

}  // namespace

TEST_F(HudLoadAlertTest, TheAlertNamesTheSavedHudStateTheHudApplied) {
    ASSERT_NO_FATAL_FAILURE(UseGameTexts(kEnglishTexts));
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "Hidden", "VR HUD: Hidden" }, { "Streamlined", "VR HUD: Streamlined" }, { "Full", "VR HUD: Full" } };
    for (const auto& [saved, expected] : cases) {
        ASSERT_NO_FATAL_FAILURE(LoadMapWithSavedHudState(saved));
        EXPECT_EQ(ShownWarnings(), std::vector<std::string>{ expected }) << "saved " << saved << ": " << Shown();
        ASSERT_NO_FATAL_FAILURE(UnloadMap());
    }
}

TEST_F(HudLoadAlertTest, UnderTheGamesGermanTextsTheAlertIsGerman) {
    ASSERT_NO_FATAL_FAILURE(UseGameTexts(kGermanTexts));
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "Hidden", "VR-HUD: Ausgeblendet" }, { "Streamlined", "VR-HUD: Reduziert" }, { "Full", "VR-HUD: Vollständig" } };
    for (const auto& [saved, expected] : cases) {
        ASSERT_NO_FATAL_FAILURE(LoadMapWithSavedHudState(saved));
        EXPECT_EQ(ShownWarnings(), std::vector<std::string>{ expected }) << "saved " << saved << ": " << Shown();
        ASSERT_NO_FATAL_FAILURE(UnloadMap());
    }
}

TEST_F(HudLoadAlertTest, WhenTheGameHasNoTextForTheKeyTheAlertIsEnglishNeverTheKey) {
    ASSERT_NO_FATAL_FAILURE(UseGameTexts("someOtherText = 'Etwas anderes'"));
    ASSERT_NO_FATAL_FAILURE(LoadMapWithSavedHudState("Streamlined"));
    EXPECT_EQ(ShownWarnings(), std::vector<std::string>{ "VR HUD: Streamlined" }) << Shown();
    ASSERT_NO_FATAL_FAILURE(UnloadMap());

    ASSERT_EQ(RunLuauChunk(L, "_G.g_i18n = nil", "=noI18n"), 0);
    ASSERT_NO_FATAL_FAILURE(LoadMapWithSavedHudState("Full"));
    EXPECT_EQ(ShownWarnings(), std::vector<std::string>{ "VR HUD: Full" }) << "a game without texts: " << Shown();
}

TEST_F(HudLoadAlertTest, TheAlertNamesNoKeyOrButton) {
    for (const char* texts : { kEnglishTexts, kGermanTexts }) {
        ASSERT_NO_FATAL_FAILURE(UseGameTexts(texts));
        for (const char* saved : { "Hidden", "Streamlined", "Full" }) {
            ASSERT_NO_FATAL_FAILURE(LoadMapWithSavedHudState(saved));
            const std::vector<std::string> shown = ShownWarnings();
            ASSERT_EQ(shown.size(), 1u) << Shown();
            for (const char* named : { "Ctrl", "Strg", "+H", "Press", "Drücke", "Taste", "key", "button", "l10n" }) {
                EXPECT_EQ(shown[0].find(named), std::string::npos) << "\"" << shown[0] << "\" names " << named;
            }
            ASSERT_NO_FATAL_FAILURE(UnloadMap());
        }
    }
}

TEST_F(HudLoadAlertTest, TheAlertIsOffByDefault) {
    ASSERT_NO_FATAL_FAILURE(UseGameTexts(kEnglishTexts));
    ASSERT_EQ(RunLuauChunk(L, "saved['vrSettings.hudState'] = 'Full'; VRCompanion:loadMap('mapUS')", "=loadMap"), 0);
    EXPECT_TRUE(ShownWarnings().empty()) << Shown();
}

TEST_F(HudLoadAlertTest, ModDescDefinesTheAlertsTextsInEnglishAndGerman) {
    const std::string modDesc = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    ASSERT_FALSE(modDesc.empty());
    const std::vector<std::tuple<std::string, std::string, std::string>> texts = {
        { "vrHud_loadAlert_hidden", "VR HUD: Hidden", "VR-HUD: Ausgeblendet" },
        { "vrHud_loadAlert_streamlined", "VR HUD: Streamlined", "VR-HUD: Reduziert" },
        { "vrHud_loadAlert_full", "VR HUD: Full", "VR-HUD: Vollständig" },
    };
    for (const auto& [key, en, de] : texts) {
        const size_t start = modDesc.find("<text name=\"" + key + "\">");
        ASSERT_NE(start, std::string::npos) << key;
        const std::string text = modDesc.substr(start, modDesc.find("</text>", start) - start);
        EXPECT_NE(text.find("<en>" + en + "</en>"), std::string::npos) << text;
        EXPECT_NE(text.find("<de>" + de + "</de>"), std::string::npos) << text;
    }
}
