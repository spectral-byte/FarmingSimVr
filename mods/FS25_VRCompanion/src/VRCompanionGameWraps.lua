-- ====================================================================
-- FS25 VR Companion Mod: game function wraps (spec #280)
--
-- The one implementation of wrapping the game's functions that the Subsystems share: counting a function's declared parameters,
-- saying why a game function is not the shape a Subsystem expects, putting a wrapper on a class once per class table (and again
-- when the game replaced the table), lifting a wrapper only while it is still the class's function (so another mod's later wrap is
-- never undone), wraps two Subsystems share, a wrap on one game object's own copy of a function, and giving up once with a warning.
--
-- A Subsystem declares the functions it wraps as a SET, and gets the checking, wrapping, lifting and giving up from here:
--
--   local wraps = VRCompanion.GameWraps.newSet({
--       owner = "objectGrab",                  -- the Subsystem's name: what holds a shared wrap
--       giveUp = "Object Grab disabled: %s. Picking up is unchanged.",   -- the warning, %s the reason ("[FS25_VR] [Warning] " before it)
--       nameAsSubject = false,                 -- reasons read "<Class.fn>: it is missing" (default) or "<Class.fn> is missing" (true)
--       functions = {                          -- keyed by a name the Subsystem uses for the function
--           <name> = {
--               class = "HandToolHands",       -- the engine global the function is on, or function() -> the table (another mod's environment)
--               className = nil,               -- the class's name in reasons, when `class` is a function
--               fn = "onDraw",                 -- the function's name on the class, when it is not <name>
--               parameters = 1,                -- how many parameters it must declare at least (a reshaped function declares fewer)
--               describe = "(tool)",           -- what they are, for the reason
--               make = function(original) return wrapper end,   -- nil: CHECK-ONLY (checked, never wrapped); may be set after newSet
--               shared = nil,                  -- the key of a shared wrap (GameWraps.defineShared) held instead of making one
--               missingClass = nil,            -- the reason when the game has no such class; nil: a missing class is not a failure
--           },
--       },
--   })
--
--   wraps:wrapAll()                 wraps each function on its class (once per class table), none once the set gave up; a function
--                                   that is not the expected shape is left alone (check reports it)
--   wraps:check() -> reason|nil     gives up (once) on the first declared function, by name, that is missing or reshaped
--   wraps:findProblem() -> reason|nil   the same reason without giving up (for a Subsystem that warns only later)
--   wraps:giveUp(reason)            lifts every wrap and logs the warning, once: a later give-up is ignored
--   wraps:problem() -> reason|nil   why the set gave up
--   wraps:liftAll()                 lifts every wrap, each only while it is still the function there; lets go of shared wraps
--   wraps:reset()                   map unload: liftAll, and forgets that the set gave up
--   wraps:wrapInstance(target, { <name>, ... })   wraps the target's own copies (a vehicle's, a tool's: the game copies type
--                                   functions onto an object), unless the class wrap reaches them; replaces a wrap of an earlier target
--   wraps:liftInstance()            gives the target its own copies back (or nothing of its own, when it had none), where still the wrapper
--   wraps:isWrapped(name), wraps:original(name)   whether the class wrap is on, and the game's function under it
--   wraps:wrapNames(), wraps:status(name)         the wrapped (not check-only) names, sorted, and "<name> wrap: installed" (or
--                                   "not installed", "gave up (<reason>)") for the diagnostics
--
--   GameWraps.parameterCount(fn) -> number|nil   nil when it takes any number, or the runtime cannot tell
--   GameWraps.shapeProblem(fn, parameters, describe, subject) -> reason|nil   "<subject> is missing", "... is not a function",
--                                   "... takes N parameters, not <describe>"; subject defaults to "it"
--   GameWraps.defineShared(key, { class, fn, make })   a wrap more than one Subsystem holds: put on with the first hold, lifted
--                                   when the last holder lets go. holdShared(key, owner), releaseShared(key, owner), sharedOriginal(key)
--
-- THE SHARED LOOK RAY: Player:getLookRay is the shared wrap "lookRay", held by Hand Tools and Object Grab, so that one giving up
-- leaves the other aiming. Its wrapper answers VRCompanion:getAimedLookRay (Core) and the game's own ray otherwise.
-- ====================================================================

VRCompanion = VRCompanion or {}

local GameWraps = {}
VRCompanion.GameWraps = GameWraps

local function engine(name)
    return VRCompanion.getEngineGlobal(name)
end

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
function GameWraps.parameterCount(fn)
    local library = _G and _G.debug
    if type(library) ~= "table" then return nil end
    if type(library.info) == "function" then
        local ok, count, isVararg = pcall(library.info, fn, "a")
        if ok and type(count) == "number" then return (not isVararg) and count or nil end
    end
    if type(library.getinfo) == "function" then
        local ok, info = pcall(library.getinfo, fn, "u")
        if ok and type(info) == "table" and type(info.nparams) == "number" then
            return (not info.isvararg) and info.nparams or nil
        end
    end
    return nil
end

-- Why fn is not a function declaring at least `parameters` parameters, or nil when it is.
function GameWraps.shapeProblem(fn, parameters, describe, subject)
    subject = subject or "it"
    if fn == nil then return subject .. " is missing" end
    if type(fn) ~= "function" then return subject .. " is not a function" end
    local count = GameWraps.parameterCount(fn)
    if count ~= nil and count < parameters then
        return string.format("%s takes %d parameters, not %s", subject, count, describe)
    end
    return nil
end

local function classOf(spec)
    local class
    if type(spec.class) == "function" then class = spec.class() else class = engine(spec.class) end
    return type(class) == "table" and class or nil
end

local function classNameOf(spec)
    return spec.className or (type(spec.class) == "string" and spec.class) or "?"
end

-- ---- Shared wraps ---------------------------------------------------------------------------------------------------------

-- sharedWraps[key] = { spec, wrap = { class, original, wrapper } | nil, holders = { [owner] = true } }
local sharedWraps = {}

function GameWraps.defineShared(key, spec)
    sharedWraps[key] = { spec = spec, wrap = nil, holders = {} }
end

local function sharedOf(key)
    local shared = sharedWraps[key]
    if shared == nil then error("no shared wrap '" .. tostring(key) .. "' is defined", 3) end
    return shared
end

-- The game's function under the shared wrap while the wrap is on `class`, or nil.
local function sharedOriginalOn(key, class)
    local wrap = sharedOf(key).wrap
    return wrap ~= nil and wrap.class == class and wrap.original or nil
end

-- Holds the shared wrap for `owner`, putting it on the class when it is not there (or the game replaced the class table). Returns
-- whether it is there.
function GameWraps.holdShared(key, owner)
    local shared = sharedOf(key)
    local class = classOf(shared.spec)
    if class == nil then return false end
    local fn = shared.spec.fn
    if shared.wrap == nil or shared.wrap.class ~= class then
        local original = class[fn]
        if type(original) ~= "function" then return false end
        local wrapper = shared.spec.make(original)
        shared.wrap = { class = class, original = original, wrapper = wrapper }
        class[fn] = wrapper
    end
    shared.holders[owner] = true
    return true
end

-- `owner` lets go; the class gets the game's function back when nobody holds the wrap any more and it is still the wrapper there.
function GameWraps.releaseShared(key, owner)
    local shared = sharedOf(key)
    shared.holders[owner] = nil
    if next(shared.holders) ~= nil or shared.wrap == nil then return end
    local wrap = shared.wrap
    shared.wrap = nil
    if wrap.class[shared.spec.fn] == wrap.wrapper then wrap.class[shared.spec.fn] = wrap.original end
end

-- The game's function under the shared wrap while it is on, or nil.
function GameWraps.sharedOriginal(key)
    local wrap = sharedOf(key).wrap
    return wrap ~= nil and wrap.original or nil
end

-- ---- A Subsystem's set of wraps ------------------------------------------------------------------------------------------

local Set = {}
Set.__index = Set

-- wraps[name] = { class, fn, original, wrapper } (or { ..., shared = key } for a held shared wrap); instance = { target, entries }
function GameWraps.newSet(declaration)
    return setmetatable({ declaration = declaration, gaveUpBecause = nil, wraps = {}, instance = nil }, Set)
end

-- The declared names, sorted, so that the first reason found does not depend on the table's order.
local function sortedNames(functions)
    local names = {}
    for name in pairs(functions) do names[#names + 1] = name end
    table.sort(names)
    return names
end

-- Wraps one declared function on its class, once per class table. A function that is not the expected shape is left alone.
function Set:wrap(name)
    local spec = self.declaration.functions[name]
    local class = classOf(spec)
    if class == nil or (spec.make == nil and spec.shared == nil) then return end
    local current = self.wraps[name]
    if current ~= nil and current.class == class then return end
    local fn = spec.fn or name
    local original = spec.shared ~= nil and sharedOriginalOn(spec.shared, class) or class[fn]
    if GameWraps.shapeProblem(original, spec.parameters, spec.describe) ~= nil then return end
    if spec.shared ~= nil then
        if GameWraps.holdShared(spec.shared, self.declaration.owner) then
            self.wraps[name] = { class = class, fn = fn, original = sharedOriginalOn(spec.shared, class), shared = spec.shared }
        end
        return
    end
    local wrapper = spec.make(original)
    self.wraps[name] = { class = class, fn = fn, original = original, wrapper = wrapper }
    class[fn] = wrapper
end

function Set:wrapAll()
    if self.gaveUpBecause ~= nil then return end
    for _, name in ipairs(sortedNames(self.declaration.functions)) do self:wrap(name) end
end

-- Gives the target its own copies back, each unless something else has replaced the wrapper since.
function Set:liftInstance()
    local current = self.instance
    self.instance = nil
    if current == nil then return end
    for _, entry in pairs(current.entries) do
        if rawget(current.target, entry.fn) == entry.wrapper then
            current.target[entry.fn] = entry.hadOwn and entry.original or nil
        end
    end
end

-- Wraps the target's own copies of the named functions (each with its declared `make`), for as long as it is the target. A copy
-- that is the class wrapper (the class wrap reaches it) or is not the expected shape is left alone; one the game replaced since
-- is wrapped again.
function Set:wrapInstance(target, names)
    if self.gaveUpBecause ~= nil then return end
    if self.instance ~= nil and self.instance.target ~= target then self:liftInstance() end
    for _, name in ipairs(names) do
        local spec = self.declaration.functions[name]
        local fn = spec.fn or name
        local entry = self.instance ~= nil and self.instance.entries[name] or nil
        if entry == nil or rawget(target, fn) ~= entry.wrapper then
            local original = target[fn]
            local classWrap = self.wraps[name]
            if GameWraps.shapeProblem(original, spec.parameters, spec.describe) == nil
                and not (classWrap ~= nil and original == classWrap.wrapper) then
                local wrapper = spec.make(original)
                self.instance = self.instance or { target = target, entries = {} }
                self.instance.entries[name] = { fn = fn, original = original, hadOwn = rawget(target, fn) ~= nil, wrapper = wrapper }
                target[fn] = wrapper
            end
        end
    end
end

-- Takes every wrap off again, each only when it is still the wrapper put there; lets go of the shared ones.
function Set:liftAll()
    self:liftInstance()
    for _, wrap in pairs(self.wraps) do
        if wrap.shared ~= nil then
            GameWraps.releaseShared(wrap.shared, self.declaration.owner)
        elseif wrap.class[wrap.fn] == wrap.wrapper then
            wrap.class[wrap.fn] = wrap.original
        end
    end
    self.wraps = {}
end

-- The reason the first declared function (by name) is missing or reshaped, or nil. A wrapped function is checked as it was
-- when wrapped (the game's, under the wrapper).
function Set:findProblem()
    local functions = self.declaration.functions
    for _, name in ipairs(sortedNames(functions)) do
        local spec = functions[name]
        local class = classOf(spec)
        if class == nil then
            if spec.missingClass ~= nil then return spec.missingClass end
        else
            local fn = spec.fn or name
            local current = self.wraps[name]
            local where = classNameOf(spec) .. "." .. fn
            local found = current ~= nil and current.original or class[fn]
            if self.declaration.nameAsSubject then
                local problem = GameWraps.shapeProblem(found, spec.parameters, spec.describe, where)
                if problem ~= nil then return problem end
            else
                local problem = GameWraps.shapeProblem(found, spec.parameters, spec.describe)
                if problem ~= nil then return where .. ": " .. problem end
            end
        end
    end
    return nil
end

-- The Subsystem cannot rely on the game's functions: every wrap lifted, ONE warning. A later give-up is ignored until reset.
function Set:giveUp(problem)
    if self.gaveUpBecause ~= nil then return end
    self.gaveUpBecause = problem
    self:liftAll()
    local template = self.declaration.giveUp
    local at = string.find(template, "%s", 1, true)
    print("[FS25_VR] [Warning] " .. string.sub(template, 1, at - 1) .. problem .. string.sub(template, at + 2))
end

function Set:check()
    if self.gaveUpBecause ~= nil then return self.gaveUpBecause end
    local problem = self:findProblem()
    if problem ~= nil then self:giveUp(problem) end
    return problem
end

function Set:problem()
    return self.gaveUpBecause
end

function Set:reset()
    self:liftAll()
    self.gaveUpBecause = nil
end

function Set:isWrapped(name)
    return self.wraps[name] ~= nil
end

function Set:original(name)
    local wrap = self.wraps[name]
    return wrap ~= nil and wrap.original or nil
end

function Set:wrapNames()
    local names = {}
    for _, name in ipairs(sortedNames(self.declaration.functions)) do
        local spec = self.declaration.functions[name]
        if spec.make ~= nil or spec.shared ~= nil then names[#names + 1] = name end
    end
    return names
end

function Set:status(name)
    if self.gaveUpBecause ~= nil then return name .. " wrap: gave up (" .. self.gaveUpBecause .. ")" end
    return name .. " wrap: " .. (self.wraps[name] ~= nil and "installed" or "not installed")
end

-- ---- The shared look ray ----------------------------------------------------------------------------------------------------

GameWraps.defineShared("lookRay", {
    class = "Player", fn = "getLookRay",
    make = function(original)
        return function(player, ...)
            if type(player) == "table" then
                local x, y, z, dx, dy, dz = VRCompanion:getAimedLookRay(player)
                if x ~= nil then return x, y, z, dx, dy, dz end
            end
            return original(player, ...)
        end
    end,
})

-- Hand Tools hold the look ray wrap through these (its WRAP_SPECS.getLookRay checks the shape itself); a set declares
-- `shared = "lookRay"` instead.
function VRCompanion:holdLookRayWrap(owner)
    return GameWraps.holdShared("lookRay", owner)
end

function VRCompanion:releaseLookRayWrap(owner)
    GameWraps.releaseShared("lookRay", owner)
end

-- The game's own Player:getLookRay while the wrap is on the class (the head's ray), or nil.
function VRCompanion:getLookRayWrapOriginal()
    return GameWraps.sharedOriginal("lookRay")
end
