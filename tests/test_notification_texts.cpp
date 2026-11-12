// The Companion Mod's notifications in the game's language (ticket #292, spec #280): every blinking warning and in-game
// notification the mod composes is looked up by a text key that modDesc.xml defines in English and German, and is today's
// English text where the game has no text for the key. Console output and log lines stay English. Seen from outside: the
// shipped Companion Mod is loaded in modDesc order, the game's texts are a stub g_i18n holding modDesc.xml's texts in one
// language (as the game holds a mod's l10n), the commands are run as the console runs them, and the test asserts what the
// game was asked to show and what the console and log.txt were given.
//
// The Seat Offset limit message is tested in test_seat_set.luau, with the scene it needs; the HUD load alert in
// test_hud_load_alert.cpp and the spray can's marker type in test_hand_tools_spray_can_feedback.cpp.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

struct ModText {
    std::string en;
    std::string de;
};

// modDesc.xml's <l10n> texts by name, each with its English and German text ("" where a language is missing).
std::map<std::string, ModText> ModDescTexts() {
    const std::string modDesc = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    std::map<std::string, ModText> texts;
    const std::regex textRe(R"re(<text name="([^"]+)">([\s\S]*?)</text>)re");
    const std::regex enRe(R"re(<en>([\s\S]*?)</en>)re");
    const std::regex deRe(R"re(<de>([\s\S]*?)</de>)re");
    for (auto it = std::sregex_iterator(modDesc.begin(), modDesc.end(), textRe); it != std::sregex_iterator(); ++it) {
        const std::string body = (*it)[2].str();
        ModText text;
        std::smatch m;
        if (std::regex_search(body, m, enRe)) text.en = m[1].str();
        if (std::regex_search(body, m, deRe)) text.de = m[1].str();
        texts[(*it)[1].str()] = text;
    }
    return texts;
}

// The mod's action names (modDesc.xml <actions>): the engine shows the text input_<action> for each by convention.
std::set<std::string> ModDescActions() {
    const std::string modDesc = ReadFile("mods/FS25_VRCompanion/modDesc.xml");
    std::set<std::string> actions;
    const std::regex actionRe(R"re(<action name="([^"]+)")re");
    for (auto it = std::sregex_iterator(modDesc.begin(), modDesc.end(), actionRe); it != std::sregex_iterator(); ++it) {
        actions.insert((*it)[1].str());
    }
    return actions;
}

// The text keys the mod's source names: string literals shaped as the mod's own keys (vr<Subsystem>_<name>) or as an action's
// text (input_<ACTION>).
std::set<std::string> KeysInModSource() {
    std::set<std::string> keys;
    const std::regex keyRe(R"re("((?:vr[A-Za-z0-9]*_[A-Za-z0-9_]+)|(?:input_[A-Z0-9_]+))")re");
    const std::filesystem::path srcDir = std::filesystem::path(PROJECT_ROOT_DIR) / "mods" / "FS25_VRCompanion" / "src";
    for (const auto& entry : std::filesystem::directory_iterator(srcDir)) {
        if (entry.path().extension() != ".lua") continue;
        const std::string source = ReadFile(entry.path().string());
        for (auto it = std::sregex_iterator(source.begin(), source.end(), keyRe); it != std::sregex_iterator(); ++it) {
            keys.insert((*it)[1].str());
        }
    }
    return keys;
}

enum class Language { English, German };

// A notification the mod shows: how to make the mod show it (Luau run against the loaded mod; the last notification shown is
// the one asserted), and what the player reads in English and in German.
struct Notification {
    std::string what;
    std::string chunk;
    std::string english;
    std::string german;
};

std::string Command(const std::string& name, const std::vector<std::string>& args) {
    std::string chunk = "_G.reply = runCommand('" + name + "'";
    for (const std::string& arg : args) chunk += ", '" + arg + "'";
    return chunk + ")";
}

// From the Hidden HUD (set with vrHudState 0, after `before`), the HUD Toggle Action pressed `presses` times: the last press's
// notification is the one asserted.
std::string CycleHud(int presses, const std::string& before = "") {
    std::string chunk = before + "\nrunCommand('vrHudState', '0')";
    for (int i = 0; i < presses; ++i) chunk += "\n_G.shown = {}\nVRCompanion:cycleHudState()";
    return chunk;
}

// In order: a fresh game runs them one after another.
const std::vector<Notification>& Notifications() {
    static const std::vector<Notification> notifications = {
        { "Hand Tools off", Command("vrHandTools", { "off" }),
          "VR Hand Tools: off (Saved)", "VR-Handwerkzeuge: aus (Gespeichert)" },
        { "Hand Tools on", Command("vrHandTools", { "on" }),
          "VR Hand Tools: on (Saved)", "VR-Handwerkzeuge: an (Gespeichert)" },
        { "Tool Hand left", Command("vrToolHand", { "left" }),
          "VR Tool Hand: left (Saved)", "VR-Werkzeughand: links (Gespeichert)" },
        { "Tool Hand right", Command("vrToolHand", { "right" }),
          "VR Tool Hand: right (Saved)", "VR-Werkzeughand: rechts (Gespeichert)" },
        { "chainsaw reach in metres", Command("vrChainsawReach", { "1.5" }),
          "VR Chainsaw reach: 1.50 m past the tip of its bar (Saved)",
          "VR-Kettensägen-Reichweite: 1.50 m über die Schwertspitze hinaus (Gespeichert)" },
        { "chainsaw reach of the game", Command("vrChainsawReach", { "default" }),
          "VR Chainsaw reach: the game's own distance (default) (Saved)",
          "VR-Kettensägen-Reichweite: die Reichweite des Spiels (Standard) (Gespeichert)" },
        { "Motion Controllers off", Command("vrMotionControllers", { "off" }),
          "VR Motion Controllers: off (Saved)", "VR-Motion-Controller: aus (Gespeichert)" },
        { "Motion Controllers on", Command("vrMotionControllers", { "on" }),
          "VR Motion Controllers: on (Saved)", "VR-Motion-Controller: an (Gespeichert)" },
        { "Object Grab off", Command("vrObjectGrab", { "off" }),
          "VR Object Grab: off (Saved)", "VR-Objektgreifen: aus (Gespeichert)" },
        { "Wheel Grab off", Command("vrWheelGrab", { "off" }),
          "VR Wheel Grab: off (Saved)", "VR-Lenkradgreifen: aus (Gespeichert)" },
        { "HUD Safe Area", Command("vrHudSafeArea", { "0.05", "0.04" }),
          "VR HUD Safe Area: Horizontal=0.05, Vertical=0.04 (Saved)",
          "VR-HUD-Sicherheitsbereich: Horizontal=0.05, Vertikal=0.04 (Gespeichert)" },
        { "HUD Group margins and scale", Command("vrHudGroup", { "map", "0.1", "0.2", "1.5" }),
          "VR HUD map group: Horizontal=0.10, Vertical=0.20, Scale=1.50x (Saved)",
          "VR-HUD-Gruppe map: Horizontal=0.10, Vertikal=0.20, Skalierung=1.50x (Gespeichert)" },
        { "HUD state set Hidden", Command("vrHudState", { "0" }), "VR HUD: Hidden (0)", "VR-HUD: Ausgeblendet (0)" },
        { "HUD state set Streamlined", Command("vrHudState", { "1" }), "VR HUD: Streamlined (1)", "VR-HUD: Reduziert (1)" },
        { "HUD state set Full", Command("vrHudState", { "2" }), "VR HUD: Full (2)", "VR-HUD: Vollständig (2)" },
        { "HUD cycled to Streamlined", CycleHud(1),
          "VR HUD: Streamlined (Speedometer)", "VR-HUD: Reduziert (Tacho)" },
        { "HUD cycled to Full", CycleHud(2),
          "VR HUD: Full (Speedometer, Map, Info, Controls)", "VR-HUD: Vollständig (Tacho, Karte, Info, Steuerung)" },
        { "HUD cycled to Hidden", CycleHud(3), "VR HUD: Hidden", "VR-HUD: Ausgeblendet" },
        { "HUD Load Notification on", Command("vrHudLoadNotification", { "true" }),
          "VR HUD: Load Notification: Enabled (Saved)", "VR-HUD: Hinweis beim Kartenladen: Aktiviert (Gespeichert)" },
        { "HUD Load Notification off", Command("vrHudLoadNotification", { "false" }),
          "VR HUD: Load Notification: Disabled (Saved)", "VR-HUD: Hinweis beim Kartenladen: Deaktiviert (Gespeichert)" },
        { "HUD Group hidden", Command("vrHudGroupShow", { "map", "full", "off" }),
          "VR HUD: Map group off in Full HUD (Saved)",
          "VR-HUD: Gruppe Karte ausgeblendet im HUD-Modus Vollständig (Gespeichert)" },
        { "HUD Group shown", Command("vrHudGroupShow", { "controls", "streamlined", "on" }),
          "VR HUD: Controls group on in Streamlined HUD (Saved)",
          "VR-HUD: Gruppe Steuerung eingeblendet im HUD-Modus Reduziert (Gespeichert)" },
        { "HUD cycled to a state with no groups",
          CycleHud(1, "runCommand('vrHudGroupShow', 'speedometer', 'streamlined', 'off')\n"
                      "runCommand('vrHudGroupShow', 'controls', 'streamlined', 'off')"),
          "VR HUD: Streamlined (none)", "VR-HUD: Reduziert (keine)" },
        { "Snap Turn angle", Command("vrSnapTurnAngle", { "45" }),
          "VR Snap Turn Angle: 45.0 deg (Saved)", "VR-Blickdrehungswinkel: 45.0° (Gespeichert)" },
        { "Stereo Presentation Mode dual-pass", Command("vrRenderingMode", { "dualpass" }),
          "Stereo Presentation Mode: Sequential Dual-Pass", "Stereo-Darstellungsmodus: Sequenzieller Dual-Pass" },
        { "Stereo Presentation Mode AFR", Command("vrRenderingMode", { "afr" }),
          "Stereo Presentation Mode: Alternate Frame Rendering (AFR)",
          "Stereo-Darstellungsmodus: Abwechselndes Frame-Rendering (AFR)" },
    };
    return notifications;
}

class NotificationTextsTest : public CompanionModTest {
protected:
    // The game at map load, with the game's texts in the language and an Injector of the version (the build's by default).
    void StartGame(Language language, const std::string& injectorVersion = "") {
        bridge_ = injectorVersion.empty() ? std::make_unique<fs25vr::VRBridge>(nullptr, nullptr)
                                          : std::make_unique<fs25vr::VRBridge>(nullptr, nullptr, injectorVersion);
        ASSERT_TRUE(bridge_->Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("xml_settings"), "=xmlStubs"), 0);
        ASSERT_NO_FATAL_FAILURE(UseGameTexts(language));
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            _G.shown = {}
            _G.g_currentMission = {
                showBlinkingWarning = function(self, text, ms) table.insert(_G.shown, text) end,
                addIngameNotification = function(self, kind, text) table.insert(_G.shown, text) end,
            }
            VRCompanion:loadMap('mapUS')
        )luau", "=missionAndMap"), 0);
    }

    // The game's texts: modDesc.xml's in the language, as the game holds a mod's l10n.
    void UseGameTexts(Language language) {
        std::string fields;
        for (const auto& [name, text] : ModDescTexts()) {
            const std::string& value = (language == Language::German) ? text.de : text.en;
            if (value.empty()) continue;
            fields += "[ [==[" + name + "]==] ] = [==[" + value + "]==],\n";
        }
        UseTexts(fields);
    }

    // The game's texts in one language, as the game's I18N has them: hasText, and getText reading "Missing ..." for a missing
    // key. Every key the mod asks for is recorded in askedKeys.
    void UseTexts(const std::string& fields) {
        const std::string chunk = "local texts = { " + fields + R"luau( }
            _G.askedKeys = {}
            _G.g_i18n = {
                hasText = function(self, name) _G.askedKeys[name] = true; return texts[name] ~= nil end,
                getText = function(self, name) _G.askedKeys[name] = true; return texts[name] or ("Missing '" .. tostring(name) .. "' in l10n") end,
            }
        )luau";
        ASSERT_EQ(RunLuauChunk(L, chunk.c_str(), "=i18n"), 0);
    }

    std::vector<std::string> StringList(const char* global) {
        std::vector<std::string> list;
        lua_getglobal(L, global);
        const int count = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            list.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return list;
    }

    std::vector<std::string> Shown() { return StringList("shown"); }

    std::set<std::string> AskedKeys() {
        RunLuauChunk(L, "_G.askedList = {}; for k in pairs(_G.askedKeys) do table.insert(_G.askedList, k) end", "=asked");
        const std::vector<std::string> list = StringList("askedList");
        return { list.begin(), list.end() };
    }

    // What the player reads for the notification: the last text shown.
    std::string ShowNotification(const Notification& n) {
        EXPECT_EQ(RunLuauChunk(L, "_G.shown = {}; _G.reply = nil\n" + n.chunk, "=notify"), 0) << n.what;
        const std::vector<std::string> shown = Shown();
        std::string all;
        for (const std::string& s : shown) all += "[" + s + "]";
        EXPECT_EQ(shown.size(), 1u) << n.what << ": " << all;
        return shown.empty() ? "" : shown.back();
    }

    std::string Reply() { return EvalLua(L, "_G.reply"); }

    std::unique_ptr<fs25vr::VRBridge> bridge_;
};

}  // namespace

TEST_F(NotificationTextsTest, UnderTheGamesEnglishTextsEachNotificationIsTodaysEnglishText) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::English));
    for (const Notification& n : Notifications()) EXPECT_EQ(ShowNotification(n), n.english) << n.what;
}

TEST_F(NotificationTextsTest, UnderTheGamesGermanTextsEachNotificationIsGerman) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::German));
    for (const Notification& n : Notifications()) EXPECT_EQ(ShowNotification(n), n.german) << n.what;

    const std::map<std::string, ModText> texts = ModDescTexts();
    for (const std::string& key : AskedKeys()) {
        if (key.rfind("vr", 0) != 0) continue;  // the game's own texts
        const auto text = texts.find(key);
        EXPECT_TRUE(text != texts.end() && !text->second.en.empty() && !text->second.de.empty())
            << "the mod asked for " << key << ", which modDesc.xml does not define in English and German";
    }
}

TEST_F(NotificationTextsTest, WhenTheGameHasNoTextForAKeyEachNotificationIsEnglishNeverTheKey) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::German));
    ASSERT_NO_FATAL_FAILURE(UseTexts("someOtherText = 'Etwas anderes'"));
    for (const Notification& n : Notifications()) EXPECT_EQ(ShowNotification(n), n.english) << n.what;
}

TEST_F(NotificationTextsTest, InAGameWithoutTextsEachNotificationIsEnglish) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::German));
    ASSERT_EQ(RunLuauChunk(L, "_G.g_i18n = nil", "=noI18n"), 0);
    for (const Notification& n : Notifications()) EXPECT_EQ(ShowNotification(n), n.english) << n.what;
}

TEST_F(NotificationTextsTest, ConsoleOutputAndLogLinesStayEnglishUnderGermanTexts) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::English));
    for (const Notification& n : Notifications()) {
        ASSERT_NO_FATAL_FAILURE(UseGameTexts(Language::English));
        s_capturedLogs.clear();
        ShowNotification(n);
        const std::string englishReply = Reply();
        const std::set<std::string> englishLog(s_capturedLogs.begin(), s_capturedLogs.end());

        ASSERT_NO_FATAL_FAILURE(UseGameTexts(Language::German));
        s_capturedLogs.clear();
        ShowNotification(n);
        EXPECT_EQ(Reply(), englishReply) << n.what << ": the console reply";
        for (const std::string& line : s_capturedLogs) {
            EXPECT_TRUE(englishLog.count(line) == 1) << n.what << ": log.txt was given " << line;
        }
    }
}

TEST_F(NotificationTextsTest, TheVersionMismatchWarningIsGermanOnScreenAndEnglishInTheLog) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::German, "7.8.9"));
    const std::string modVersion = EvalLua(L, "VRCompanion.VERSION");
    EXPECT_EQ(Shown(), std::vector<std::string>{ "FS25 VR: Die Companion Mod ist Version " + modVersion +
                                                 ", der Injector aber Version 7.8.9. Installiere beide Dateien aus demselben Release." });
    const std::string englishLine = "[FS25_VR] [Warning] The Companion Mod is version " + modVersion +
                                    " but the Injector is version 7.8.9. Install both files from the same release.";
    EXPECT_EQ(std::count(s_capturedLogs.begin(), s_capturedLogs.end(), englishLine), 1) << "log.txt keeps the English line";
}

TEST_F(NotificationTextsTest, TheVersionMismatchWarningNamesAnOlderInjectorInGerman) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::German, "1.0.0"));
    const std::string modVersion = EvalLua(L, "VRCompanion.VERSION");
    EXPECT_EQ(Shown(), std::vector<std::string>{ "FS25 VR: Die Companion Mod ist Version " + modVersion +
                                                 ", der Injector aber eine ältere Version. Installiere beide Dateien aus demselben Release." });
}

TEST_F(NotificationTextsTest, TheVersionMismatchWarningIsEnglishWhereTheGameHasNoText) {
    ASSERT_NO_FATAL_FAILURE(StartGame(Language::English, "7.8.9"));
    const std::string modVersion = EvalLua(L, "VRCompanion.VERSION");
    EXPECT_EQ(Shown(), std::vector<std::string>{ "FS25 VR: The Companion Mod is version " + modVersion +
                                                 " but the Injector is version 7.8.9. Install both files from the same release." });
}

TEST_F(NotificationTextsTest, EveryKeyTheModNamesIsInModDescInEnglishAndGerman) {
    const std::map<std::string, ModText> texts = ModDescTexts();
    const std::set<std::string> keys = KeysInModSource();
    ASSERT_FALSE(keys.empty());
    for (const std::string& key : keys) {
        const auto text = texts.find(key);
        ASSERT_TRUE(text != texts.end()) << "the mod names " << key << ", which modDesc.xml does not define";
        EXPECT_FALSE(text->second.en.empty()) << key << " has no English text";
        EXPECT_FALSE(text->second.de.empty()) << key << " has no German text";
    }
}

TEST_F(NotificationTextsTest, EveryTextInModDescIsUsed) {
    const std::set<std::string> keys = KeysInModSource();
    const std::set<std::string> actions = ModDescActions();
    for (const auto& [name, text] : ModDescTexts()) {
        const bool actionText = name.rfind("input_", 0) == 0 && actions.count(name.substr(6)) == 1;
        EXPECT_TRUE(actionText || keys.count(name) == 1) << "modDesc.xml defines " << name << ", which nothing uses";
    }
}
