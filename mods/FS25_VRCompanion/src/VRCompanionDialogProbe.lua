-- ====================================================================
-- FS25 VR Companion Mod: Dialog probe
--
-- A diagnostic console command for Stereo Dialogs (spec #142, ADR-0032): while a game dialog is open it writes what the game shows
-- to the game log, so that a headset run on a real fish catch (the Highlands Fishing DLC's Lua is encrypted) tells what the
-- Stereo Dialog support needs: the dialog's GUI name, how sell and release are answered (a callback or a method), which input
-- actions answer it, which fish fields it holds (species, weight, price) and where its root sits. Read-only: nothing is called,
-- nothing is written to the game's tables, and any field that cannot be read becomes a line, never an error.
--
-- The game's GUI (Gui.lua): g_gui.currentGuiName is the open SCREEN's name (a dialog does not set it), g_gui.dialogs holds the open
-- dialogs' root GuiElements in the order they opened, and each root's `target` is the dialog's controller object, whose class chain
-- holds the methods (the sell and release handlers, the base dialog's). The newest dialog is the one the player answers.
--
-- PUBLIC SURFACE
--   VRCompanion:getDialogDiagnostics() -> lines   the lines the command writes, without the tag ("no dialog open" when none is)
--   VRCompanion:registerDialogDiagnosticsConsoleCommands() / resetDialogDiagnostics()   map load / unload
--   Console: vrDialogDiagnostics   logs, tagged "[FS25_VR] dialog probe: " in the game log (Documents\My Games\FarmingSimulator2025\log.txt):
--     g_gui.currentGuiName and currentDialogName, the open dialogs' names, the newest dialog's name and class, its class chain
--     (each class's method names), its root element's fields and its target's own fields (one level deep, functions by name), and
--     every position, size, margin or offset field of the root and of the target's elements, the yes and no buttons and the text
--     element dumped whole (their input action and click handler), and every long string in full (the dialog's text holds the catch)
--
-- The dump's readers are the Hand Tool probe's (VRCompanionHandTools.lua: VRCompanion.HandTools.probeReaders).
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.DialogProbe = VRCompanion.DialogProbe or {}

local engine = VRCompanion.getEngineGlobal  -- VRCompanionCore.lua, loaded before this file
local readers = VRCompanion.HandTools.probeReaders  -- VRCompanionHandTools.lua, loaded before this file
local shortValue, sortedKeys, functionNames, globalNameOf = readers.shortValue, readers.sortedKeys, readers.functionNames, readers.globalNameOf

local TAG = "[FS25_VR] dialog probe: "

-- The caps. Each cut says how much was left out.
local MAX_LINES = 1000          -- lines per run, the line that says how many were left out included
local MAX_FIELDS = 400          -- fields shown per table
local MAX_NESTED_KEYS = 40      -- a table field with more keys than this is only counted, not opened
local MAX_CLASS_LEVELS = 8      -- levels of the class chain
local MAX_POSITION_ELEMENTS = 20  -- elements of the target whose position is shown
local MAX_COMPACT_ENTRIES = 8   -- entries of a position table shown on one line
local SHORT_TEXT = 30           -- a string longer than this is cut by the short values, so it is also logged in full
local MAX_FULL_TEXT = 1000      -- characters of one full text

-- The target's elements dumped whole: the answer buttons (their input action, click handler) and the text the player reads.
local DUMPED_ELEMENTS = { "yesButton", "noButton", "dialogTextElement" }

local POSITION_WORDS = { "pos", "size", "margin", "offset", "anchor", "pivot", "scale", "align", "width", "height" }

local function isPositionKey(key)
    if type(key) ~= "string" then return false end
    local lower = string.lower(key)
    for _, word in ipairs(POSITION_WORDS) do
        if string.find(lower, word, 1, true) ~= nil then return true end
    end
    return false
end

-- One short line of text for a value: as the Hand Tool probe shows it, and a small table (a position is two numbers) with its
-- entries, sorted by key.
local function compactValue(value)
    if type(value) ~= "table" then return shortValue(value) or type(value) end
    local keys = sortedKeys(value)
    if #keys == 0 then return "{}" end
    if #keys > MAX_COMPACT_ENTRIES then return #keys .. " keys" end
    local parts = {}
    for _, key in ipairs(keys) do
        local entry = rawget(value, key)
        local shown = type(entry) == "table" and (#sortedKeys(entry) .. " keys") or (shortValue(entry) or type(entry))
        parts[#parts + 1] = (type(key) == "number" and "" or (tostring(key) .. " = ")) .. shown
    end
    return "{ " .. table.concat(parts, ", ") .. " }"
end

-- The name the dialog's class has: the engine's own answer, else the global the class is kept under.
local function classNameOf(object, chain)
    local classUtil = engine("ClassUtil")
    if type(classUtil) == "table" and type(classUtil.getClassNameByObject) == "function" then
        local ok, name = pcall(classUtil.getClassNameByObject, object)
        if ok and type(name) == "string" and name ~= "" then return name end
    end
    for _, level in ipairs(chain) do
        if level.globalName ~= nil then return level.globalName end
    end
    return nil
end

-- The class chain of an object: the table its metatable sends lookups to, then that table's own metatable's, and so on up to the
-- base class. Each level is { class = table, metatable = table, globalName = text|nil, className = text|nil }.
local function classChain(object)
    local chain, seen = {}, {}
    local current = object
    for _ = 1, MAX_CLASS_LEVELS do
        local metatable = getmetatable(current)
        if type(metatable) ~= "table" or seen[metatable] then break end
        seen[metatable] = true
        local index = rawget(metatable, "__index")
        local class = type(index) == "table" and index or metatable
        if seen[class] and class ~= metatable then break end
        seen[class] = true
        local className = rawget(class, "className")
        chain[#chain + 1] = {
            class = class, metatable = metatable, globalName = globalNameOf(class),
            className = type(className) == "string" and className or nil,
        }
        current = class
    end
    return chain
end

-- Writes a table's own fields: a line per field with its name, Lua type and short value, a small table field's entries one
-- level further, and the function names in one line (a function is a field like any other, by name). Reads with rawget, so no
-- __index of the game's runs.
local function dumpTable(label, tbl, add)
    -- Plain values (numbers, strings, booleans: the fish's species, weight and price) come before tables, so that a dialog with
    -- hundreds of element tables is cut among those, not among the values.
    local plain, rest = {}, {}
    for _, key in ipairs(sortedKeys(tbl)) do
        local kind = type(rawget(tbl, key))
        if kind == "number" or kind == "string" or kind == "boolean" or kind == "function" then plain[#plain + 1] = key else rest[#rest + 1] = key end
    end
    local keys = plain
    for _, key in ipairs(rest) do keys[#keys + 1] = key end
    local functions = {}
    for index, key in ipairs(keys) do
        if index > MAX_FIELDS then
            add(string.format("%s: %d more fields left out", label, #keys - MAX_FIELDS))
            break
        end
        local ok, err = pcall(function()
            local value = rawget(tbl, key)
            if type(value) == "function" then
                functions[#functions + 1] = tostring(key)
                return
            end
            local shown = shortValue(value)
            add(string.format("%s.%s (%s)%s", label, tostring(key), type(value), shown ~= nil and (" = " .. shown) or ""))
            if type(value) == "table" then
                local nested = sortedKeys(value)
                if #nested > 0 and #nested <= MAX_NESTED_KEYS then
                    for _, nestedKey in ipairs(nested) do
                        local nestedValue = rawget(value, nestedKey)
                        local nestedShown = shortValue(nestedValue)
                        add(string.format("%s.%s.%s (%s)%s", label, tostring(key), tostring(nestedKey), type(nestedValue),
                            nestedShown ~= nil and (" = " .. nestedShown) or ""))
                    end
                end
            end
        end)
        if not ok then add(string.format("%s.%s could not be read (%s)", label, tostring(key), tostring(err))) end
    end
    if #functions > 0 then
        table.sort(functions)
        add(string.format("%s functions: %s", label, table.concat(functions, ", ")))
    end
end

-- The strings of a table that dumpTable's short values cut (a dialog's text holds the catch: species, weight, price), whole, one
-- line each, capped at MAX_FULL_TEXT characters.
local function fullTexts(label, tbl, add)
    for _, key in ipairs(sortedKeys(tbl)) do
        local value = rawget(tbl, key)
        if type(value) == "string" and #value > SHORT_TEXT then
            add(string.format('%s.%s (full) = "%s"', label, tostring(key), string.sub(value, 1, MAX_FULL_TEXT)))
        end
    end
end

-- The position-related fields of a table (position, absPosition, size, margin, offsets, anchors...), one line.
local function positionLine(label, tbl)
    local parts = {}
    for _, key in ipairs(sortedKeys(tbl)) do
        if isPositionKey(key) then
            local value = rawget(tbl, key)
            if type(value) ~= "function" then parts[#parts + 1] = string.format("%s = %s", tostring(key), compactValue(value)) end
        end
    end
    if #parts == 0 then return nil end
    return string.format("%s position fields: %s", label, table.concat(parts, "; "))
end

-- Every part runs on its own: a failure becomes a line, and the other parts go on.
local function guarded(what, part, add)
    local ok, err = pcall(part)
    if not ok then add(what .. " could not be read (" .. tostring(err) .. ")") end
end

local function readField(tbl, key)
    if type(tbl) ~= "table" then return nil end
    local ok, value = pcall(rawget, tbl, key)
    return ok and value or nil
end

-- The newest open dialog's root element, and how many there are, or nil when no dialog is open.
local function openDialogs()
    local gui = engine("g_gui")
    if type(gui) ~= "table" then return nil, 0, gui end
    local dialogs = readField(gui, "dialogs")
    if type(dialogs) ~= "table" or #dialogs == 0 then return nil, 0, gui end
    return dialogs[#dialogs], #dialogs, gui
end

local function quoted(value)
    return value ~= nil and (type(value) == "string" and ('"' .. value .. '"') or tostring(value)) or "nil"
end

local function describeDialogs()
    local lines, leftOut = {}, 0
    local function add(line)
        if #lines < MAX_LINES then lines[#lines + 1] = line else leftOut = leftOut + 1 end
    end

    local root, count, gui = openDialogs()
    if root == nil then
        add("no dialog open")
        return lines
    end

    guarded("g_gui", function()
        add("g_gui.currentGuiName: " .. quoted(readField(gui, "currentGuiName")))
        add("g_gui.currentDialogName: " .. quoted(readField(gui, "currentDialogName")))
        local names = {}
        for _, dialog in ipairs(readField(gui, "dialogs")) do names[#names + 1] = quoted(readField(dialog, "name")) end
        add(string.format("dialogs open: %d (%s), the newest is the one answered", count, table.concat(names, ", ")))
    end, add)

    local target = readField(root, "target")
    local chain = {}
    guarded("class chain", function() chain = type(target) == "table" and classChain(target) or {} end, add)

    guarded("dialog name", function() add("dialog name: " .. quoted(readField(root, "name"))) end, add)
    guarded("dialog class", function()
        add("dialog class: " .. (type(target) == "table" and (classNameOf(target, chain) or "(unknown)") or "(the root has no target)"))
    end, add)
    guarded("class chain", function()
        for index, level in ipairs(chain) do
            add(string.format("class chain %d: %s%s, functions: %s", index,
                level.className or level.globalName or "(unnamed)",
                (level.className ~= nil and level.globalName ~= nil) and (" (global " .. level.globalName .. ")") or "",
                functionNames(level.class)))
            if level.metatable ~= level.class then add(string.format("class chain %d metatable functions: %s", index, functionNames(level.metatable))) end
        end
    end, add)

    guarded("root position", function()
        local line = positionLine("root", root)
        add(line or "root position fields: none")
    end, add)
    guarded("target elements", function()
        if type(target) ~= "table" then return end
        local shown = 0
        for _, key in ipairs(sortedKeys(target)) do
            local value = rawget(target, key)
            if type(value) == "table" and type(rawget(value, "absPosition")) == "table" and shown < MAX_POSITION_ELEMENTS then
                shown = shown + 1
                add((positionLine("target." .. tostring(key), value)) or ("target." .. tostring(key) .. " position fields: none"))
            end
        end
    end, add)
    guarded("target", function() if type(target) == "table" then dumpTable("target", target, add) end end, add)
    for _, name in ipairs(DUMPED_ELEMENTS) do
        guarded("target." .. name, function()
            local element = readField(target, name)
            if type(element) == "table" then
                dumpTable("target." .. name, element, add)
                fullTexts("target." .. name, element, add)
            end
        end, add)
    end
    guarded("target texts", function() if type(target) == "table" then fullTexts("target", target, add) end end, add)
    guarded("root", function() dumpTable("root", root, add) end, add)

    if leftOut > 0 then lines[#lines] = string.format("%d more lines left out", leftOut + 1) end
    return lines
end

function VRCompanion:getDialogDiagnostics()
    local ok, lines = pcall(describeDialogs)
    if ok then return lines end
    return { "could not be read: " .. tostring(lines) }
end

function VRCompanion:consoleCommandDialogDiagnostics()
    local lines = self:getDialogDiagnostics()
    for _, line in ipairs(lines) do
        print(TAG .. line)
    end
    return lines[1] == "no dialog open" and "no dialog open" or string.format("dialog probe: %d lines in the game log", #lines)
end

function VRCompanion:registerDialogDiagnosticsConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrDialogDiagnostics", "Log the open dialog's GUI name, class, methods and fields to the game log (read-only; says so when no dialog is open): vrDialogDiagnostics", "consoleCommandDialogDiagnostics", self)
end

function VRCompanion:resetDialogDiagnostics()
    if removeConsoleCommand ~= nil then removeConsoleCommand("vrDialogDiagnostics") end
end

VRCompanion.DialogProbe.getDialogDiagnostics = function(self, ...) return VRCompanion.getDialogDiagnostics(VRCompanion, ...) end
VRCompanion.DialogProbe.consoleCommandDialogDiagnostics = function(self, ...) return VRCompanion.consoleCommandDialogDiagnostics(VRCompanion, ...) end

return VRCompanion.DialogProbe

