VRCompanion = VRCompanion or {}

local GameWraps = {}
VRCompanion.GameWraps = GameWraps

local function engine(name)
    return VRCompanion.getEngineGlobal(name)
end

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

local Set = {}
Set.__index = Set

function GameWraps.newSet(declaration)
    return setmetatable({ declaration = declaration, gaveUpBecause = nil, wraps = {} }, Set)
end

local function sortedNames(functions)
    local names = {}
    for name in pairs(functions) do names[#names + 1] = name end
    table.sort(names)
    return names
end

local function classOf(spec)
    local class
    if type(spec.class) == "function" then class = spec.class() else class = engine(spec.class) end
    return type(class) == "table" and class or nil
end

local function classNameOf(spec)
    return spec.className or (type(spec.class) == "string" and spec.class) or "?"
end

local sharedWraps = {}

function GameWraps.defineShared(key, spec)
    sharedWraps[key] = { spec = spec, wrap = nil, holders = {} }
end

local function sharedOf(key)
    local shared = sharedWraps[key]
    if shared == nil then error("no shared wrap '" .. tostring(key) .. "' is defined", 3) end
    return shared
end

-- The game's function under the shared wrap while it is on `class`.
local function sharedOriginal(key, class)
    local wrap = sharedOf(key).wrap
    return wrap ~= nil and wrap.class == class and wrap.original or nil
end

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

function GameWraps.releaseShared(key, owner)
    local shared = sharedOf(key)
    shared.holders[owner] = nil
    if next(shared.holders) ~= nil or shared.wrap == nil then return end
    local wrap = shared.wrap
    shared.wrap = nil
    if wrap.class[shared.spec.fn] == wrap.wrapper then wrap.class[shared.spec.fn] = wrap.original end
end

function GameWraps.sharedOriginal(key)
    local wrap = sharedOf(key).wrap
    return wrap ~= nil and wrap.original or nil
end

function Set:wrap(name)
    local spec = self.declaration.functions[name]
    local class = classOf(spec)
    if class == nil or (spec.make == nil and spec.shared == nil) then return end
    local current = self.wraps[name]
    if current ~= nil and current.class == class then return end
    local fn = spec.fn or name
    local original = spec.shared ~= nil and sharedOriginal(spec.shared, class) or class[fn]
    if GameWraps.shapeProblem(original, spec.parameters, spec.describe) ~= nil then return end
    if spec.shared ~= nil then
        if GameWraps.holdShared(spec.shared, self.declaration.owner) then
            self.wraps[name] = { class = class, fn = fn, original = sharedOriginal(spec.shared, class), shared = spec.shared }
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
