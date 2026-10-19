// Stereo Dialogs, the probe (spec #142, ticket #143): the console command vrDialogDiagnostics logs what the game shows while a
// dialog is open, so that a headset run on a real fish catch (Highlands Fishing DLC, whose Lua is encrypted) tells what the rest
// of the spec needs: the dialog's GUI name, how sell and release are answered, which input actions answer it, which fish fields it
// holds and where its root sits. Seen from outside: the real Companion Mod runs with the game stood in by Lua stubs (the console
// and g_gui as the engine's Gui.lua keeps them: `dialogs` holds the open dialogs' root elements, each with a `target` that is the
// dialog's controller object), the test runs the command as the game would and asserts what the developer reads in the game log
// and what the command answers. Never the mod's own state.
// Prior art: test_hand_tools_describe_unsupported.cpp (a probe's log lines), test_virtual_gamepad_menu_use.cpp (g_gui stubs).

#include "companion_mod_fixture.h"
#include <string>
#include <vector>

namespace {

// Every line the command writes starts with this.
constexpr const char* kTag = "[FS25_VR] dialog probe: ";

// The engine's console and the game's GUI. A dialog is the root GuiElement in g_gui.dialogs; its `target` is the controller
// object (Gui:loadGui, Gui.lua), whose class chain holds the methods. `showFishDialog` opens a stand-in for the fish catch
// dialog: its own fields (the fish), a class with the answer handlers and a parent class with the base dialog's methods.
constexpr const char* kStubs = R"luau(
    _G.registeredCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.registeredCommands[name] = nil end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end

    _G.ClassUtil = {
        getClassNameByObject = function(object)
            local mt = getmetatable(object)
            return mt ~= nil and mt.__index ~= nil and mt.__index.className or nil
        end,
    }
    _G.calls = 0
    _G.DialogBase = { className = "MessageDialog" }
    DialogBase.__index = DialogBase
    function DialogBase:onClickBack() _G.calls = _G.calls + 1 end
    function DialogBase:close() _G.calls = _G.calls + 1 end
    _G.FishDialog = setmetatable({ className = "FishCatchDialog" }, { __index = DialogBase })
    FishDialog.__index = FishDialog
    function FishDialog:onClickSell() _G.calls = _G.calls + 1 end
    function FishDialog:onClickRelease() _G.calls = _G.calls + 1 end

    _G.g_gui = {
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsDialogVisible = function(self) return #self.dialogs > 0 end,
        getIsGuiVisible = function(self) return self.currentGui ~= nil or #self.dialogs > 0 end,
    }

    _G.showFishDialog = function()
        local target = setmetatable({
            name = "FishCatchDialog",
            speciesText = "Pike",
            weight = 3.25,
            price = 120,
            isSold = false,
            fish = { species = "Pike", weight = 3.25, price = 120 },
            sellCallback = function() _G.calls = _G.calls + 1 end,
        }, FishDialog)
        local root = {
            name = "FishCatchDialog",
            target = target,
            position = { 0.3, 0.4 },
            absPosition = { 0.31, 0.41 },
            size = { 0.4, 0.2 },
            absSize = { 0.4, 0.2 },
        }
        target.dialogElement = root
        table.insert(g_gui.dialogs, root)
        g_gui.currentDialogName = "FishCatchDialog"
        return root
    end
)luau";

// The mod started with the stubs in place (the suite is CompanionModTest itself, so the Companion Mod filter covers it).
void Start(lua_State* L) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubs, "=dialogStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// Runs the command as the game would; the log lines are what the game logged during it. Returns the command's answer.
std::string RunCommand(lua_State* L) {
    s_capturedLogs.clear();
    return EvalLua(L, "runCommand('vrDialogDiagnostics')");
}
// The lines the command wrote, without the tag.
std::vector<std::string> Lines() {
    std::vector<std::string> lines;
    const std::string tag = kTag;
    for (const auto& line : s_capturedLogs) {
        if (line.rfind(tag, 0) == 0) lines.push_back(line.substr(tag.size()));
    }
    return lines;
}
std::string Joined(const std::vector<std::string>& lines) {
    std::string all;
    for (const auto& line : lines) all += line + "\n";
    return all;
}
// The first line that has all of the texts, or "" when there is none.
std::string LineWith(const std::vector<std::string>& lines, std::initializer_list<const char*> texts) {
    for (const auto& line : lines) {
        bool all = true;
        for (const char* text : texts) all = all && line.find(text) != std::string::npos;
        if (all) return line;
    }
    return "";
}
}  // namespace

TEST_F(CompanionModTest, WithTheFishDialogOpenTheCommandLogsTheGuiNameAndTheDialogsNameAndClass) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "g_gui.currentGuiName = 'GameScreen'; showFishDialog()", "=open"), 0);

    RunCommand(L);

    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "g_gui.currentGuiName", "\"GameScreen\"" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "name", "\"FishCatchDialog\"" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "class", "FishCatchDialog" }).empty()) << Joined(lines);
}


// How sell and release are answered, which input actions answer the dialog, and the fish fields: the dialog's own table, and the
// method names of its class chain (the base dialog's too), which is where the DLC's handlers are.
TEST_F(CompanionModTest, TheCommandLogsTheDialogsOwnFieldsOneLevelDeepAndTheMethodNamesOfItsClassChain) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=open"), 0);

    RunCommand(L);

    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "target.speciesText (string)", "\"Pike\"" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.weight (number)", "3.25" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.price (number)", "120" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.fish (table)", "3 keys" }).empty()) << "a table by its key count: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.fish.species (string)", "\"Pike\"" }).empty()) << "one level further: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target functions", "sellCallback" }).empty()) << "functions by name: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "class chain 1", "onClickSell", "onClickRelease" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "class chain 2", "MessageDialog", "onClickBack", "close" }).empty()) << "the base class too: " << Joined(lines);
}

// The root element's position, so that the headset round can tell whether the whole dialog can be moved by it.
TEST_F(CompanionModTest, TheCommandLogsTheRootElementsPositionAndSizeFields) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=open"), 0);

    RunCommand(L);

    const auto lines = Lines();
    const std::string line = LineWith(lines, { "root position fields" });
    EXPECT_NE(line.find("position = { 0.3, 0.4 }"), std::string::npos) << Joined(lines);
    EXPECT_NE(line.find("absPosition = { 0.31, 0.41 }"), std::string::npos) << Joined(lines);
    EXPECT_NE(line.find("size = { 0.4, 0.2 }"), std::string::npos) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "root.name (string)", "\"FishCatchDialog\"" }).empty()) << "the root's own table too: " << Joined(lines);
}

TEST_F(CompanionModTest, WithNoDialogOpenTheCommandSaysSoAndLogsNothingElse) {
    ASSERT_NO_FATAL_FAILURE(Start(L));

    const std::string answer = RunCommand(L);

    EXPECT_EQ(answer, "no dialog open");
    const auto lines = Lines();
    ASSERT_EQ(lines.size(), 1u) << Joined(lines);
    EXPECT_EQ(lines[0], "no dialog open");
    EXPECT_EQ(s_capturedLogs.size(), 1u) << "nothing else is logged";

    // A menu screen without a dialog, and a dialog list that is gone, are no dialog either.
    ASSERT_EQ(RunLuauChunk(L, "g_gui.currentGuiName = 'InGameMenu'", "=menu"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
    ASSERT_EQ(RunLuauChunk(L, "g_gui.dialogs = nil", "=noList"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui = nil", "=noGui"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
}

// Read-only and tolerant: the dialog's fields can be anything. A table that holds itself, a huge table, odd keys, a missing target,
// a table that raises on every read, an enormous string and far too many fields never make an error, and the log is capped.
TEST_F(CompanionModTest, TheCommandIsReadOnlyAndNeverErrorsOnOddCyclicOrHugeFields) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local root = showFishDialog()
        local target = root.target
        target.selfReference = target
        target.parentLoop = { root = root, target = target }
        target.fish.owner = target
        local huge = {}
        for i = 1, 5000 do huge[i] = i end
        target.huge = huge
        for i = 1, 600 do target["field" .. i] = i end
        target[true] = "boolean key"
        target[{}] = "table key"
        target[1.5] = "number key"
        target.long = string.rep("x", 100000)
        target.proxy = setmetatable({}, { __index = function() error("read raised") end, __len = function() error("len raised") end })
        target.unreadable = setmetatable({ a = 1 }, { __index = function() error("read raised") end })
        local protected = setmetatable({}, { __metatable = "locked" })
        target.locked = protected
        root.absPosition = "not a table"
        root.size = nil
        _G.calls = 0
    )luau", "=oddFields"), 0);

    const std::string answer = RunCommand(L);

    EXPECT_NE(answer, "<error>");
    const auto lines = Lines();
    EXPECT_FALSE(lines.empty());
    EXPECT_LE(lines.size(), 1000u) << "the log is capped";
    EXPECT_FALSE(LineWith(lines, { "target:", "more fields left out" }).empty()) << "and says so: " << Joined(lines);
    EXPECT_EQ(EvalLua(L, "_G.calls"), "0") << "no method of the dialog was called";
    EXPECT_EQ(EvalLua(L, "g_gui.dialogs[1].target.fish.price"), "120") << "nothing of the dialog was changed";
}

TEST_F(CompanionModTest, ADialogWithoutATargetOrNamesStillGivesALogNotAnError) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "g_gui.dialogs = { { } }; _G.ClassUtil = nil", "=bareDialog"), 0);

    const std::string answer = RunCommand(L);

    EXPECT_NE(answer, "<error>");
    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "dialog name: nil" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "dialog class", "no target" }).empty()) << Joined(lines);
}

// Without the engine's ClassUtil the class is named by the global it is kept under.
TEST_F(CompanionModTest, WithoutTheEnginesClassUtilTheClassIsNamedByTheGlobalItIsKeptUnder) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "_G.ClassUtil = nil; showFishDialog()", "=open"), 0);

    RunCommand(L);

    EXPECT_FALSE(LineWith(Lines(), { "dialog class: FishDialog" }).empty()) << Joined(Lines());
}

TEST_F(CompanionModTest, TheCommandIsRegisteredOnMapLoadAndRemovedOnMapUnload) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrDialogDiagnostics'] ~= nil"), "true");
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "registeredCommands['vrDialogDiagnostics'] == nil"), "true");
}


// A real dialog controller holds hundreds of element tables next to its few values: the values come first, so a cut leaves the
// fish's species, weight and price in the log.
TEST_F(CompanionModTest, ADialogWithManyElementFieldsStillLogsItsPlainValuesBeforeTheCut) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local root = showFishDialog()
        for i = 1, 600 do root.target["aElement" .. i] = { id = i, absPosition = { 0, 0 } } end
    )luau", "=manyElements"), 0);

    RunCommand(L);

    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "target.price (number)", "120" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "more", "left out" }).empty()) << "says what it left out: " << Joined(lines);
}


// The headset's first run (a generic yes/no dialog: SELL and RELEASE buttons, the catch only in a long text the log cut short):
// the full text, and the answer buttons' own fields (their input action, their click handler) are what the next step needs.
TEST_F(CompanionModTest, TheCommandLogsTheFullTextsAndTheAnswerButtonsOwnFields) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local root = showFishDialog()
        local target = root.target
        target.defaultText = "Do you want to release the fish? Species: Pike, weight: 3.25 kg, price: 120"
        local function button(inputAction)
            local b = { inputActionName = inputAction, onClickCallback = function() end, text = "A button label that is long, too" }
            b.inputActions = { "MENU_ACCEPT", "MENU_ACTIVATE" }
            for i = 1, 100 do b["padding" .. i] = i end
            return b
        end
        target.yesButton = button("MENU_ACCEPT")
        target.noButton = button("MENU_BACK")
        target.dialogTextElement = { text = "The text the player reads: Pike, 3.25 kg, sells for 120", absPosition = { 0, 0 } }
    )luau", "=buttons"), 0);

    RunCommand(L);

    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "target.defaultText", "full", "Species: Pike, weight: 3.25 kg, price: 120" }).empty())
        << "a long text in full: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.dialogTextElement.text", "full", "sells for 120" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.yesButton.inputActionName (string)", "MENU_ACCEPT" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.noButton.inputActionName (string)", "MENU_BACK" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.yesButton.inputActions.1", "MENU_ACCEPT" }).empty()) << "a button's table one level further: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "target.yesButton functions", "onClickCallback" }).empty()) << Joined(lines);
}
