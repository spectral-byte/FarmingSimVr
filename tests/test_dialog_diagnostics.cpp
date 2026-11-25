#include "companion_mod_fixture.h"
#include <string>
#include <vector>

namespace {

constexpr const char* kTag = "[FS25_VR] dialog probe: ";

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

void Start(lua_State* L) {
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubs, "=dialogStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

std::string RunCommand(lua_State* L) {
    s_capturedLogs.clear();
    return EvalLua(L, "runCommand('vrDialogDiagnostics')");
}
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
std::string LineWith(const std::vector<std::string>& lines, std::initializer_list<const char*> texts) {
    for (const auto& line : lines) {
        bool all = true;
        for (const char* text : texts) all = all && line.find(text) != std::string::npos;
        if (all) return line;
    }
    return "";
}
}

TEST_F(CompanionModTest, WithTheFishDialogOpenTheCommandLogsTheGuiNameAndTheDialogsNameAndClass) {
    ASSERT_NO_FATAL_FAILURE(Start(L));
    ASSERT_EQ(RunLuauChunk(L, "g_gui.currentGuiName = 'GameScreen'; showFishDialog()", "=open"), 0);

    RunCommand(L);

    const auto lines = Lines();
    EXPECT_FALSE(LineWith(lines, { "g_gui.currentGuiName", "\"GameScreen\"" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "name", "\"FishCatchDialog\"" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "class", "FishCatchDialog" }).empty()) << Joined(lines);
}


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

    ASSERT_EQ(RunLuauChunk(L, "g_gui.currentGuiName = 'InGameMenu'", "=menu"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
    ASSERT_EQ(RunLuauChunk(L, "g_gui.dialogs = nil", "=noList"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
    ASSERT_EQ(RunLuauChunk(L, "_G.g_gui = nil", "=noGui"), 0);
    EXPECT_EQ(RunCommand(L), "no dialog open");
}

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
