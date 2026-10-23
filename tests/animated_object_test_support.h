// What the Animated Objects tests (tests/test_animated_objects*.cpp, ADR-0034, spec #180) share, on top of object_grab_test_support.h (the
// player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): the game's
// Animated Objects as a stub (StubAnimatedObject, shaped after AnimatedObject, AnimatedObjectActivatable and PlaceableAnimatedObjects in the
// SDK's gameSource.zip), the engine's raycastClosest over the boxes of its parts, and a fixture. Header-only (inline), in its own
// namespace; a test file brings it in with `using namespace animated_object_test;` inside its anonymous namespace.
//
// The stub is a chunk of its own (kStubAnimatedObjects), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs
// (AnimatedObjectStubs(variant)): it runs after the Companion Mod's files are loaded and before the map is, so the mod finds the game's
// classes when it looks. A door is made with StubAnimatedObject.door(options) after the scene exists (the zone is entered by the local
// player's root node, the node the scene's player stands on).
//
// StubAnimatedObject.install(variant)   defines the globals the game has: AnimatedObject (the class: triggerCallback, getCanBeTriggered,
//   setDirection, raiseActive), AnimatedMapObject (a subclass that defines none of its own, like the game's, for a barrier placed on the
//   map), AnimatedObjectActivatable (getIsActivatable, onAnimationInputToggle, updateActionEventTexts), FSCareerMissionInfo and
//   g_currentMission.missionInfo:isa, g_currentMission.activatableObjectsSystem (addActivatable / removeActivatable keep
//   StubAnimatedObject.activatables), CollisionFlag / CollisionMask, and the engine's raycastClosest.
//   variant   how the game is in this "game version": nil / "ok", "triggerCallbackMissing" (a game update renamed triggerCallback, which
//   the engine calls by the new name), "triggerCallbackReshaped" (it takes no parameters but self: the engine's arguments are read from
//   StubAnimatedObject.pending), "toggleMissing" (onAnimationInputToggle renamed), "toggleReshaped" (it takes no parameters at all),
//   "canBeTriggeredMissing" (getCanBeTriggered renamed), "canBeTriggeredReshaped" (it takes no parameters at all).
//
// StubAnimatedObject.door(options) -> object   an Animated Object with its zone (a trigger node) and one moving part, a box at the
//   world position x, y, z with the half extents hx, hy, hz (defaults: a leaf 1 m wide, 1 m high and 10 cm thick).
//   options: name, x, y, z, hx, hy, hz, map (an AnimatedMapObject, not an AnimatedObject), canBeTriggered (false: the check answers no),
//   farmCheck (the object has an own getCanBeTriggered, like PlaceableAnimatedObjects, that also asks object.farmAllowed),
//   negAction (a second action: the object moves while a button is held), sameAction (one action for both directions: the same),
//   noAction (no action at all: no prompt), collisionChild (the box is on a child of the part's node, as a shape below a transform
//   group is), parts (a list of { x, y, z, hx, hy, hz } instead of the one part).
//   object.controls = { posAction, negAction, posActionText, negActionText }, object.animation = { parts = { { node } }, time, direction
//   (0 stopped, 1 opening, -1 closing), duration }, object.activatable, object.triggerNode, object.isEnabled, object.isDeleted.
// StubAnimatedObject.enterZone(object) / leaveZone(object)   the local player enters or leaves the zone: the engine calls the trigger
//   callback, by the name the game has it, with the zone's trigger node and the local player's root node.
//   StubAnimatedObject.otherEnters(object)   another node enters, which the callback does not count.
// StubAnimatedObject.inZone(object)   the game put the object's activatable in its system (the prompt can show it).
// StubAnimatedObject.wall(name, x, y, z, hx, hy, hz) -> node   a box of the world the ray cast hits and no Animated Object owns.
// StubAnimatedObject.showPrompt(object) / pressButton(object)   the game's prompt: it registers the toggle handler on the bound button
//   (controls.posActionEventId is set from then on) and the button calls what was registered.
// StubAnimatedObject.toggles   every run of the toggle handler, { object, direction }, in order; toggleCount(object)
// StubAnimatedObject.advance(object, fraction)   the animation moves by that part of its duration in its direction, and stops (direction
//   0, as the game's server sets it) at either end. setDirection decides a stopped door's direction by its time, as the game's does.
// StubAnimatedObject.originalTriggerCallback   the game's trigger callback as the stub made it (the class has it unless a wrap replaced it).
// StubAnimatedObject.castSeesLooseObjects   (default true) the raycastClosest also hits the loose objects of the hands stubs
//   (StubHandTool.looseObjectList, spheres), as the engine reports any rigid body: an object in front of a door hides the door from it.
// StubAnimatedObject.casts   every raycastClosest call, { x, y, z, dx, dy, dz, maxDistance, mask }.
// StubAnimatedObject.delete(object)   the object is deleted with no leave event for the zone.
// StubAnimatedObject.textCalls   the prompt texts set, { eventId }; setting one for an event that is not registered raises, as the game's
//   input binding does for a nil id.

#pragma once

#include "object_grab_test_support.h"
#include <string>

inline constexpr const char* kStubAnimatedObjects = R"luau(
    StubAnimatedObject = {}
    local S = StubAnimatedObject

    local nextBox = 0
    function S.install(variant)
        variant = variant or "ok"
        S.toggles, S.casts, S.textCalls, S.activatables, S.registered, S.boxList, S.boxes = {}, {}, {}, {}, {}, {}, {}
        S.callbackName, S.toggleName, S.canName, S.pending, S.raised = "triggerCallback", "onAnimationInputToggle", "getCanBeTriggered", nil, 0
        S.castSeesLooseObjects = true

        _G.CollisionFlag = { DEFAULT = 1, STATIC_OBJECT = 2, DYNAMIC_OBJECT = 4, PLAYER = 8, TRIGGER = 16, TERRAIN = 32, TERRAIN_DELTA = 64,
            TERRAIN_DISPLACEMENT = 128, FILLABLE = 256, BUILDING = 512 }
        _G.CollisionMask = { ALL = 4294967295 }

        local FSCareerMissionInfo = {}
        _G.FSCareerMissionInfo = FSCareerMissionInfo
        _G.g_currentMission = _G.g_currentMission or {}
        _G.g_currentMission.missionInfo = { isa = function(self, class) return class == FSCareerMissionInfo end }
        _G.g_currentMission.activatableObjectsSystem = {
            addActivatable = function(self, activatable) S.activatables[activatable] = true end,
            removeActivatable = function(self, activatable) S.activatables[activatable] = nil end,
        }

        local AnimatedObject = {}
        AnimatedObject.__index = AnimatedObject
        _G.AnimatedObject = AnimatedObject
        -- The subclass the game makes map objects of: it defines no trigger callback or check of its own.
        local AnimatedMapObject = setmetatable({}, { __index = AnimatedObject })
        AnimatedMapObject.__index = AnimatedMapObject
        _G.AnimatedMapObject = AnimatedMapObject
        local Activatable = {}
        Activatable.__index = Activatable
        _G.AnimatedObjectActivatable = Activatable
        S.classes = { AnimatedObject = AnimatedObject, AnimatedMapObject = AnimatedMapObject, Activatable = Activatable }

        -- AnimatedObject.lua:730 -- the three conditions, then the activatable goes in or out of the system
        local function triggerCallback(self, triggerId, otherId, onEnter, onLeave, onStay)
            if g_currentMission.missionInfo:isa(FSCareerMissionInfo) then
                if onEnter or onLeave then
                    if g_localPlayer ~= nil and otherId == g_localPlayer.rootNode then
                        if onEnter then
                            g_currentMission.activatableObjectsSystem:addActivatable(self.activatable)
                        else
                            g_currentMission.activatableObjectsSystem:removeActivatable(self.activatable)
                        end
                        self:raiseActive()
                    end
                end
            end
        end
        if variant == "triggerCallbackMissing" then
            AnimatedObject.onTriggerCallback = triggerCallback
            S.callbackName = "onTriggerCallback"
        elseif variant == "triggerCallbackReshaped" then
            AnimatedObject.triggerCallback = function(self)
                local a = S.pending
                triggerCallback(self, a[1], a[2], a[3], a[4], a[5])
            end
        else
            AnimatedObject.triggerCallback = triggerCallback
        end
        function AnimatedObject:raiseActive() S.raised = S.raised + 1 end
        S.originalTriggerCallback = AnimatedObject.triggerCallback  -- what the game has, for a test to see that it is handed back as found

        -- the game's check (opening hours and the like; its body is stripped from the SDK): a test flips object.canBeTriggered
        local function canBeTriggered(self) return self.canBeTriggered ~= false end
        if variant == "canBeTriggeredMissing" then
            AnimatedObject.getIsTriggerable = canBeTriggered
            S.canName = "getIsTriggerable"
        elseif variant == "canBeTriggeredReshaped" then
            AnimatedObject.getCanBeTriggered = function() return true end
        else
            AnimatedObject.getCanBeTriggered = canBeTriggered
        end

        -- setDirection decides a stopped door's direction by its time (the handler passes 0 * -1 then)
        function AnimatedObject:setDirection(direction)
            local animation = self.animation
            if direction == 0 then direction = animation.time >= 0.5 and -1 or 1 end
            animation.direction = direction
        end

        -- AnimatedObjectActivatable.lua
        function Activatable:getIsActivatable()
            return self.animatedObject[S.canName](self.animatedObject) == true and self.animatedObject.isEnabled ~= false
        end
        local function toggle(self)
            local object = self.animatedObject
            local direction = object.animation.direction * -1
            table.insert(S.toggles, { object = object, direction = direction })
            object:setDirection(direction)
            self:updateActionEventTexts()
        end
        if variant == "toggleMissing" then
            Activatable.onAnimationToggle = toggle
            S.toggleName = "onAnimationToggle"
        elseif variant == "toggleReshaped" then
            Activatable.onAnimationInputToggle = function() toggle(S.pressTarget) end
        else
            Activatable.onAnimationInputToggle = toggle
        end
        function Activatable:updateActionEventTexts()
            local controls = self.animatedObject.controls
            if controls.posAction and not controls.negAction and controls.posActionText ~= nil and controls.negActionText ~= nil then
                S.setActionEventText(controls.posActionEventId, controls.posActionText)
            end
        end
        -- g_inputBinding:setActionEventText: raises for an event that is not registered
        function S.setActionEventText(eventId, text)
            if eventId == nil then error("setActionEventText: no action event") end
            table.insert(S.textCalls, { eventId = eventId, text = text })
        end

        -- the engine's raycastClosest over the boxes: the closest box the ray enters from outside within maxDistance. The direction is unit.
        _G.raycastClosest = function(x, y, z, dx, dy, dz, maxDistance, callbackName, target, mask)
            table.insert(S.casts, { x = x, y = y, z = z, dx = dx, dy = dy, dz = dz, maxDistance = maxDistance, mask = mask })
            local best, bestNode = nil, nil
            for _, node in ipairs(S.boxList) do
                local box = S.boxes[node]
                if StubScene.nodes[node] ~= nil then
                    local ox, oy, oz = worldToLocal(node, x, y, z)
                    local ex, ey, ez = worldToLocal(node, x + dx, y + dy, z + dz)
                    local d = { ex - ox, ey - oy, ez - oz }
                    local o, h = { ox, oy, oz }, { box.hx, box.hy, box.hz }
                    local tmin, tmax, miss = -math.huge, math.huge, false
                    for i = 1, 3 do
                        if math.abs(d[i]) < 1e-9 then
                            if math.abs(o[i]) > h[i] then miss = true end
                        else
                            local t1, t2 = (-h[i] - o[i]) / d[i], (h[i] - o[i]) / d[i]
                            if t1 > t2 then t1, t2 = t2, t1 end
                            tmin, tmax = math.max(tmin, t1), math.min(tmax, t2)
                        end
                    end
                    if not miss and tmin <= tmax and tmin >= 0 and tmin <= maxDistance and (best == nil or tmin < best) then
                        best, bestNode = tmin, node
                    end
                end
            end
            -- the loose objects of the hands stubs are rigid bodies too, and the engine's cast reports them (a pallet in front of a door
            -- hides the door from it) unless a test turns that off to leave the choice to the nearer-wins rule alone
            local list = S.castSeesLooseObjects and StubHandTool and StubHandTool.looseObjectList or {}
            for _, object in ipairs(list) do
                if entityExists(object.node) then
                    local cx, cy, cz = getWorldTranslation(object.node)
                    local ox, oy, oz = cx - x, cy - y, cz - z
                    local b = ox * dx + oy * dy + oz * dz
                    local disc = b * b - (ox * ox + oy * oy + oz * oz - object.radius ^ 2)
                    if disc >= 0 then
                        local t = b - math.sqrt(disc)
                        if t >= 0 and t <= maxDistance and (best == nil or t < best) then best, bestNode = t, object.node end
                    end
                end
            end
            if best == nil then return 0 end
            target[callbackName](target, bestNode, x + dx * best, y + dy * best, z + dz * best, best, 0, 0, 1, 0, 0, true)
            return 1
        end
    end

    local function newBox(name, parent, x, y, z, hx, hy, hz)
        local node = StubScene.newNode(name, parent, x, y, z)
        S.boxes[node] = { hx = hx, hy = hy, hz = hz }
        table.insert(S.boxList, node)
        return node
    end

    function S.wall(name, x, y, z, hx, hy, hz) return newBox(name, 0, x, y, z, hx, hy, hz) end

    local doors = 0
    function S.door(options)
        options = options or {}
        doors = doors + 1
        local name = options.name or ("door" .. doors)
        local class = options.map and S.classes.AnimatedMapObject or S.classes.AnimatedObject
        local object = setmetatable({ saveId = name, isEnabled = true, canBeTriggered = options.canBeTriggered ~= false }, class)
        object.controls = { posAction = "ACTIVATE_HANDTOOL", negAction = options.negAction, posActionText = "Open", negActionText = "Close" }
        if options.noAction then object.controls.posAction = nil end
        if options.sameAction then object.controls.negAction = object.controls.posAction end
        object.animation = { parts = {}, time = 0, direction = 0, duration = 2000 }
        object.triggerNode = StubScene.newNode(name .. "Trigger", 0)
        object.activatable = setmetatable({ animatedObject = object }, S.classes.Activatable)
        for _, part in ipairs(options.parts or { { x = options.x, y = options.y, z = options.z, hx = options.hx, hy = options.hy, hz = options.hz } }) do
            local px, py, pz = part.x or 0, part.y or 0, part.z or 2
            local hx, hy, hz = part.hx or 0.5, part.hy or 0.5, part.hz or 0.05
            local node
            if options.collisionChild then
                node = StubScene.newNode(name .. "Part", 0, px, py, pz)
                newBox(name .. "PartShape", node, 0, 0, 0, hx, hy, hz)
            else
                node = newBox(name .. "Part", 0, px, py, pz, hx, hy, hz)
            end
            table.insert(object.animation.parts, { node = node })
        end
        if options.farmCheck then
            local super = object[S.canName]
            object.farmAllowed = true
            object[S.canName] = function(self) return super(self) and self.farmAllowed ~= false end
        end
        return object
    end

    local function playerRootNode()
        local player = g_localPlayer
        if player.rootNode == nil then player.rootNode = (_G.foot and _G.foot.parent) or StubScene.newNode("playerRoot", 0) end
        return player.rootNode
    end
    -- the engine calls the callback by name, on the object (through its class)
    local function callTrigger(object, otherId, onEnter, onLeave)
        S.pending = { object.triggerNode, otherId, onEnter, onLeave, false }
        object[S.callbackName](object, object.triggerNode, otherId, onEnter, onLeave, false)
    end
    function S.enterZone(object) callTrigger(object, playerRootNode(), true, false) end
    function S.leaveZone(object) callTrigger(object, playerRootNode(), false, true) end
    function S.otherEnters(object) callTrigger(object, StubScene.newNode("someoneElse", 0), true, false) end
    function S.inZone(object) return S.activatables[object.activatable] == true end
    function S.delete(object) object.isDeleted = true end

    function S.showPrompt(object)
        object.controls.posActionEventId = 100 + #S.registered + 1
        table.insert(S.registered, { object = object, callback = object.activatable[S.toggleName] })
    end
    function S.pressButton(object)
        for _, entry in ipairs(S.registered) do
            if entry.object == object then
                S.pressTarget = object.activatable
                entry.callback(object.activatable)
                return
            end
        end
        error("the prompt is not showing")
    end

    function S.toggleCount(object)
        local count = 0
        for _, toggle in ipairs(S.toggles) do
            if toggle.object == object then count = count + 1 end
        end
        return count
    end
    function S.advance(object, fraction)
        local animation = object.animation
        if animation.direction == 0 then return end
        animation.time = math.max(0, math.min(1, animation.time + animation.direction * fraction))
        if animation.time <= 0 or animation.time >= 1 then animation.direction = 0 end
    end
)luau";

namespace animated_object_test {

using namespace object_grab_test;

// The game's Animated Objects (kStubAnimatedObjects), for StartOptions::extraStubs. variant: see the header above.
inline std::string AnimatedObjectStubs(const char* variant = "ok") {
    return std::string(kStubAnimatedObjects) + "\nStubAnimatedObject.install('" + variant + "')\n";
}

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same
// geometry as the Object Grab tests). The door's leaf is straight ahead of it, 1.65 m from the fingertip.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
// Lua helper: animatedCasts() are the ray casts along the Pointer Ray (to its length and the cast starting behind the fingertip), the ones
// Object Grab's check of the off-hand does not make.
constexpr const char* kAnimatedObjectHelpers = R"luau(
    function _G.animatedCasts()
        local length = VRCompanion.POINTER_RAY_LENGTH + VRCompanion.ANIMATED_OBJECT_REACH_BEHIND
        local casts = {}
        for _, cast in ipairs(StubAnimatedObject.casts) do
            if math.abs(cast.maxDistance - length) < 1e-6 then table.insert(casts, cast) end
        end
        return casts
    end
)luau";
constexpr const char* kDoorAhead = "_G.door = StubAnimatedObject.door({ name = 'door', x = -0.4, y = -0.2, z = 2.0 })";

// A test of the Pointer Ray working Animated Objects, seen from what the game reads from the Virtual Gamepad: the mod started on a
// player on foot with the game's empty hands, the Virtual Gamepad on, the game clock set, and the game's Animated Objects in the game.
// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) covers.
class VirtualGamepadAnimatedObjectsTest : public VirtualGamepadObjectGrabTest {
protected:
    // The game with its Animated Objects ("ok" or a variant), and the door of kDoorAhead unless `withDoor` is false.
    void StartWithDoor(const char* variant = "ok", bool withDoor = true) {
        stubs_ = AnimatedObjectStubs(variant);
        StartOptions options;
        options.extraStubs = stubs_.c_str();
        ASSERT_NO_FATAL_FAILURE(Start(options));
        ASSERT_EQ(RunLuauChunk(L, kAnimatedObjectHelpers, "=animatedObjectHelpers"), 0);
        if (withDoor) ASSERT_EQ(RunLuauChunk(L, kDoorAhead, "=door"), 0);
    }

    void EnterZone(const char* object = "door") { ASSERT_EQ(RunLuauChunk(L, (std::string("StubAnimatedObject.enterZone(") + object + ")").c_str(), "=enterZone"), 0); }
    void LeaveZone(const char* object = "door") { ASSERT_EQ(RunLuauChunk(L, (std::string("StubAnimatedObject.leaveZone(") + object + ")").c_str(), "=leaveZone"), 0); }

    // How many times the toggle handler ran, in all or for one object.
    int Toggles() { return static_cast<int>(Number(L, "#StubAnimatedObject.toggles")); }
    int Toggles(const char* object) { return static_cast<int>(Number(L, std::string("StubAnimatedObject.toggleCount(") + object + ")")); }

    void PullTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(1.0f)); FRAMES(1); }
    void ReleaseTrigger(uint32_t hand) { headset.SetHandInputs(hand, Trigger(0.0f)); FRAMES(1); }

    // Whether the Pointer Ray is drawn now, and the end of the line that was drawn ("x y z" to two decimals, or "none").
    std::string DrawnEnd() {
        return EvalLua(L, "(function() local x, y, z = drawnRayEnd(); return x and string.format('%.2f %.2f %.2f', x, y, z) or 'none' end)()");
    }

    // One count of the module's warnings in the log.
    int Warnings() { return CountLogLines("Animated Objects support disabled"); }

private:
    std::string stubs_;
};

}  // namespace animated_object_test
