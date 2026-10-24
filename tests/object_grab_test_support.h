// What the Object Grab tests (tests/test_object_grab_*.cpp, ADR-0033) share, on top of hand_tools_test_support.h (the player on
// foot, the frame the game runs before the Companion Mod's update, the fake hands): the game's empty hands as a stub
// (StubHandTool.hands, shaped after HandToolHands in the SDK's gameSource.zip), loose objects with a mass that its targeter can hit
// (StubHandTool.looseObject), the Lua helpers that read the rays, and the macros that point a Motion Controller. Header-only
// (inline), in its own namespace; a test file brings it in with `using namespace object_grab_test;` inside its anonymous namespace.
//
// The stubs are a chunk of their own (kStubHandToolHands), like the Hand Tools' tool chunks, so that the later Object Grab tickets
// extend it without touching the Hand Tools' stubs. A test passes HandsStubs(variant) as StartOptions::extraStubs (concatenated
// after kXmlStubs, if it uses both), and the player's hands are taken by HoldHands (the game's empty hands are Player.hands, held while
// the player holds no Hand Tool: Player:getHeldHandTool() is nil then, and answers the Hand Tool once one is held).
//
// StubHandTool.installHands(variant)   defines the global HandToolHands (the class the game's hands are made of; TARGET_MASK,
//   and the events onDraw / onUpdate / onHeldStart as the game's event dispatch reads them off the class at every call), and makes
//   the targeter honour target types (PlayerTargeter:addTargetType / :removeTargetType, :getLastLookRay, unless the spray can's
//   chunk gave them already). Each update the targeter fills closestTargetsByKey[HandToolHands] with the closest loose object the
//   look ray meets between the type's minimum (0.5 m) and maximum distance (spec.pickupDistance), measured from the ray's origin,
//   as { node, x, y, z, distance }. LIKE THE GAME'S POOL it is ONE table per targeter that the next update overwrites: a mod that
//   keeps a reference across frames reads another frame's target. Install it AFTER the other chunks that extend the targeter.
//   variant   how the class is in this "game version": nil / "ok", "drawMissing" (a game update renamed onDraw to onDrawOverlays,
//   which the draw calls instead), "drawReshaped" (onDraw takes no parameters), "canPickUpMissing" (getCanPickUpNode is gone;
//   the hands tool has getCanPickUpTarget instead), "canPickUpReshaped" (it takes no node).
//
// StubHandTool.hands(player, options) -> tool   the hands tool (spec_hands), player.hands, not yet held. Like the game's registered functions,
//   updateKinematicNode, getKinematicNode, getIsHoldingItem, getCanPickUpNode, orientRotationNodeToItem, pickUpTarget,
//   dropHeldItem, throwHeldItemWithForceVector and onHeldItemJointBroken are COPIED onto the tool when it is made (a wrap of the
//   class does not reach this tool; its events go through the class at every call). tool.spec_hands: hasSuperStrength (false),
//   currentMaximumMass (StubHandTool.HANDS.MAXIMUM_MASS), pickupDistance (StubHandTool.HANDS.PICKUP_DISTANCE), heldItemNode,
//   heldItemJointId (nil: the stub makes no joint), currentHoldDistance, kinematicNode, kinematicRotationNode (a child of it).
//   spec_hands.dirtyFlag (1) with tool:raiseDirtyFlags(flag) recorded in StubHandTool.dirtyRaised ({ tool, flag }); tool.isServer
//   (true, a single player game); tool:getIsAwaitingJointCreation() (spec_hands.awaitingJoint, false: the game's server makes the joint
//   a few physics frames after the pick-up, and until then the rotation node is not at the object); the engine's setJointFrame
//   (jointId, frame, node) recorded in StubHandTool.jointFrames ({ jointId, frame, node }). The stub makes no joint: a test sets
//   spec_hands.heldItemJointId itself.
//   Holding it (StubHandTool.hold) adds its target type to the player's targeter (0.5 m to pickupDistance), putting it away
//   (StubHandTool.drop) removes it. It is NOT Player.currentHandTool: getHeldHandTool() stays nil. Holding another tool puts the held
//   hands away, putting that tool away takes them again.
// StubHandTool.superStrength(tool, on)   the game's gsPlayerSuperStrengthToggle: the mass limit rises to
//   StubHandTool.HANDS.SUPER_STRENGTH_MASS, the pickup distance to SUPER_STRENGTH_DISTANCE (10 m), and the target type is added
//   again. The object's own getCanBePickedUp is not asked under super strength.
// StubHandTool.looseObject(x, y, z, mass, options) -> object   a loose object (a sphere of options.radius, 0.15 m, with a node
//   at its centre) at the world position. object.node, object.mass (the game's unit, tonnes), object.canBePickedUp (true: false
//   is an object that refuses the pick-up, like a strapped-down one). StubHandTool.looseObjects[node] -> object.
// The hands tool's methods, as the game has them and as a test sees what they did:
//   getCanPickUpNode(node)   false for no node, a node that is gone, while holding something, a mass over currentMaximumMass, or an
//     object that refuses (unless super strength)
//   pickUpTarget(target)   false unless getCanPickUpNode(target.node); otherwise holds it (heldItemNode, currentHoldDistance =
//     target.distance), puts the rotation node at the object's centre and the kinematic node along the look ray; recorded in
//     StubHandTool.pickUps as { tool, node, x, y, z, distance }
//   dropHeldItem(noEventSend)   recorded in StubHandTool.drops ({ tool, held = node }) when something was held; lets go
//   throwHeldItemWithForceVector(dirX, dirY, dirZ, force, noEventSend)   recorded in StubHandTool.throws ({ tool, node, dirX,
//     dirY, dirZ, force }); lets go
//   onHeldItemJointBroken(jointIndex, impulse, noEventSend)   what the game's joint break report calls: recorded in
//     StubHandTool.breaks ({ tool, jointIndex }); lets go. dropHeldItem is the game's own letting go: it passes jointIndex nil.
//   updateKinematicNode()   the game's (nothing unless the hands are held and the player is the owner): while holding, the kinematic node goes to the look ray's origin plus its direction times
//     currentHoldDistance, turned to the ray's heading (level); run each tick after the targeter for a held hands tool
// StubHandTool.pressPickUp(tool)   the game's pick-up button: pickUpTarget with the targeter's closest target for the hands, when
//   getCanPickUpNode accepts it. StubHandTool.breakJoint(tool): the held item's joint breaks (onHeldItemJointBroken(1, 500)).
//
// The draw: StubHandTool.draw() renders "crosshair" (nothing under the centre), "pickUpCrosshair" (holding, or the hands' target
// is one getCanPickUpNode accepts), for the owner in the first person view, as the game's onDraw does; StubHandTool.rendering(tool).

#pragma once

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

inline constexpr const char* kStubHandToolHands = R"luau(
    StubHandTool.looseObjects, StubHandTool.looseObjectList = {}, {}
    StubHandTool.pickUps, StubHandTool.drops, StubHandTool.throws, StubHandTool.breaks = {}, {}, {}, {}
    StubHandTool.dirtyRaised, StubHandTool.jointFrames = {}, {}
    StubHandTool.HANDS = { MAXIMUM_MASS = 0.1, PICKUP_DISTANCE = 3, SUPER_STRENGTH_MASS = 1000, SUPER_STRENGTH_DISTANCE = 10 }

    function StubHandTool.looseObject(x, y, z, mass, options)
        options = options or {}
        local node = StubScene.newNode(options.name or "looseObject", 0, x, y, z)
        local object = { node = node, mass = mass, radius = options.radius or 0.15, canBePickedUp = true }
        object.getCanBePickedUp = function(self, player) return self.canBePickedUp end
        StubHandTool.looseObjects[node] = object
        table.insert(StubHandTool.looseObjectList, object)
        return object
    end

    -- the closest loose object a ray meets between a minimum and a maximum distance of its origin (a sphere hit)
    local function castAtLooseObjects(into, x, y, z, dx, dy, dz, minDistance, maxDistance)
        local length = math.sqrt(dx * dx + dy * dy + dz * dz)
        if length < 1e-9 then return nil end
        dx, dy, dz = dx / length, dy / length, dz / length
        local best, bestObject = nil, nil
        for _, object in ipairs(StubHandTool.looseObjectList) do
            if entityExists(object.node) then
                local cx, cy, cz = getWorldTranslation(object.node)
                local ox, oy, oz = cx - x, cy - y, cz - z
                local b = ox * dx + oy * dy + oz * dz
                local disc = b * b - (ox * ox + oy * oy + oz * oz - object.radius ^ 2)
                if disc >= 0 then
                    local t = b - math.sqrt(disc)
                    if t >= minDistance and t <= maxDistance and (best == nil or t < best) then best, bestObject = t, object end
                end
            end
        end
        if best == nil then return nil end
        into.node, into.x, into.y, into.z, into.distance = bestObject.node, x + dx * best, y + dy * best, z + dz * best, best
        return into
    end
    StubHandTool.castAtLooseObjects = castAtLooseObjects  -- for the stubs of other features whose ray cast meets the same objects

    function StubHandTool.installHands(variant)
        variant = variant or "ok"
        StubHandTool.pickUps, StubHandTool.drops, StubHandTool.throws, StubHandTool.breaks = {}, {}, {}, {}
        StubHandTool.dirtyRaised, StubHandTool.jointFrames = {}, {}
        local Class = { TARGET_MASK = 1 }
        _G.HandToolHands = Class
        -- the engine's setJointFrame(jointId, frameIndex, node): the joint's frame goes to the node's world pose
        _G.setJointFrame = function(jointId, frame, node) table.insert(StubHandTool.jointFrames, { jointId = jointId, frame = frame, node = node }) end
        local H = StubHandTool.HANDS

        -- ---- the targeter: target types with a minimum and a maximum distance, the last look ray
        local PlayerTargeter = _G.PlayerTargeter
        if PlayerTargeter.addTargetType == nil then
            function PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance)
                self.targetedMasks = self.targetedMasks or {}
                if self.targetedMasks[key] ~= nil then return end
                self.targetedMasks[key] = { key = key, mask = mask, minDistance = minDistance or 0, maxDistance = maxDistance }
            end
            function PlayerTargeter:removeTargetType(key)
                if self.targetedMasks ~= nil then self.targetedMasks[key] = nil end
                if self.closestTargetsByKey ~= nil then self.closestTargetsByKey[key] = nil end
            end
        end
        if PlayerTargeter.getLastLookRay == nil then
            function PlayerTargeter:getLastLookRay()
                return self.lastRayX, self.lastRayY, self.lastRayZ, self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ
            end
        end
        if PlayerTargeter.getClosestTargetedNodeFromType == nil then
            function PlayerTargeter:getClosestTargetedNodeFromType(key)
                local target = (self.closestTargetsByKey or {})[key]
                return target ~= nil and target.node or nil
            end
        end
        local baseUpdate = PlayerTargeter.update
        function PlayerTargeter:update(dt)
            baseUpdate(self, dt)
            self.closestTargetsByKey = self.closestTargetsByKey or {}
            self.closestTargetsByKey[Class] = nil
            local mask = (self.targetedMasks or {})[Class]
            if mask ~= nil and self.lastRayX ~= nil then
                self.pooledHandsTarget = self.pooledHandsTarget or {}
                self.closestTargetsByKey[Class] = castAtLooseObjects(self.pooledHandsTarget, self.lastRayX, self.lastRayY, self.lastRayZ,
                    self.lastRayDirectionX, self.lastRayDirectionY, self.lastRayDirectionZ, mask.minDistance, mask.maxDistance)
            end
        end

        -- ---- the class: the functions the game registers on the hands tool's type (copied onto each tool)
        local registered = {}
        function registered.getIsHoldingItem(self) return self.spec_hands.heldItemNode ~= nil end
        function registered.getKinematicNode(self) return self.spec_hands.kinematicNode, self.spec_hands.kinematicRotationNode end
        local function canPickUpNode(self, node)
            local spec = self.spec_hands
            if node == nil or node == 0 or not entityExists(node) then return false end
            local object = StubHandTool.looseObjects[node]
            if object == nil then return false end
            if self:getIsHoldingItem() then return false end
            if object.mass > spec.currentMaximumMass then return false end
            if not spec.hasSuperStrength and not object:getCanBePickedUp(self:getCarryingPlayer()) then return false end
            return true
        end
        if variant == "canPickUpMissing" then
            registered.getCanPickUpTarget = canPickUpNode
            Class.getCanPickUpTarget = canPickUpNode
        elseif variant == "canPickUpReshaped" then
            registered.getCanPickUpNode = function() return false end
            Class.getCanPickUpNode = registered.getCanPickUpNode
        else
            registered.getCanPickUpNode = canPickUpNode
            Class.getCanPickUpNode = canPickUpNode
        end
        function registered.getIsAwaitingJointCreation(self) return self.spec_hands.awaitingJoint == true end
        function registered.orientRotationNodeToItem(self, node)
            local spec = self.spec_hands
            local x, y, z = getWorldTranslation(node)
            setWorldTranslation(spec.kinematicRotationNode, x, y, z)
            local qx, qy, qz, qw = getWorldQuaternion(node)
            setWorldQuaternion(spec.kinematicRotationNode, qx, qy, qz, qw)
        end
        function registered.updateKinematicNode(self)
            local spec = self.spec_hands
            local player = self:getCarryingPlayer()
            if spec.kinematicNode == nil or not self:getIsHeld() or not self:getIsHoldingItem() or player == nil or not player.isOwner then return end
            local x, y, z, dx, dy, dz = player.targeter:getLastLookRay()
            if x == nil then return end
            self:raiseDirtyFlags(spec.dirtyFlag)
            setWorldTranslation(spec.kinematicNode, x + dx * spec.currentHoldDistance, y + dy * spec.currentHoldDistance,
                z + dz * spec.currentHoldDistance)
            local yaw = math.atan2(dx, dz)
            setWorldQuaternion(spec.kinematicNode, 0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
        end
        function registered.pickUpTarget(self, target, noEventSend)
            local spec = self.spec_hands
            if target == nil or not self:getCanPickUpNode(target.node) then return false end
            table.insert(StubHandTool.pickUps, { tool = self, node = target.node, x = target.x, y = target.y, z = target.z,
                distance = target.distance })
            spec.heldItemNode = target.node
            spec.currentHoldDistance = target.distance
            self:updateKinematicNode()
            self:orientRotationNodeToItem(target.node)
            return true
        end
        function registered.dropHeldItem(self, noEventSend)
            local spec = self.spec_hands
            if spec.heldItemNode == nil then return end
            table.insert(StubHandTool.drops, { tool = self, held = spec.heldItemNode })
            spec.heldItemNode = nil
        end
        function registered.throwHeldItemWithForceVector(self, dirX, dirY, dirZ, force, noEventSend)
            local spec = self.spec_hands
            if spec.heldItemNode == nil then return end
            table.insert(StubHandTool.throws, { tool = self, node = spec.heldItemNode, dirX = dirX, dirY = dirY, dirZ = dirZ, force = force })
            spec.heldItemNode = nil
        end
        function registered.onHeldItemJointBroken(self, jointIndex, breakingImpulse, noEventSend)
            table.insert(StubHandTool.breaks, { tool = self, jointIndex = jointIndex })
            self.spec_hands.heldItemNode = nil
        end
        for name, fn in pairs(registered) do Class[name] = fn end
        StubHandTool.handsRegistered = registered

        -- ---- the events, read off the class at every call
        local function onDraw(self)
            local spec = self.spec_hands
            local player = self:getCarryingPlayer()
            if player ~= nil and player.isOwner and player.camera.isFirstPerson then
                local target = player.targeter.closestTargetsByKey[Class]
                local node = target and target.node or nil
                if spec.heldItemNode ~= nil or self:getCanPickUpNode(node) then
                    StubHandTool.render(self, "pickUpCrosshair")
                else
                    StubHandTool.render(self, "crosshair")
                end
            end
        end
        if variant == "drawMissing" then
            Class.onDrawOverlays = onDraw
        elseif variant == "drawReshaped" then
            Class.onDraw = function() StubHandTool.render(StubHandTool.hands_, "crosshair") end
        else
            Class.onDraw = onDraw
        end
        StubHandTool.originalHandsOnDraw = Class.onDraw
        table.insert(StubHandTool.drawers, function(tool)
            if tool.spec_hands ~= nil then
                local draw = Class.onDraw or Class.onDrawOverlays
                if draw ~= nil then draw(tool) end
            end
        end)
        table.insert(StubHandTool.updaters, function(dt)
            for _, tool in ipairs(StubHandTool.tools) do
                if tool.spec_hands ~= nil and tool.isHeld then tool:updateKinematicNode() end
            end
        end)

        -- ---- the hands are held when no Hand Tool is (the game: Player.hands is held and Player:getHeldHandTool() is nil; taking a
        -- Hand Tool puts the hands away, putting it away takes them again). Held hands target loose objects, put away they stop
        -- (the game's onHeldStart / onHeldEnd). Player.currentHandTool is never the hands.
        local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop
        local function startHands(hands)
            hands.isHeld, hands.carryingPlayer = true, hands.player
            StubHandTool.heldTool = hands
            hands:attachTool()
            if hands.player.isOwner then
                hands.player.targeter:addTargetType(Class, Class.TARGET_MASK, 0.5, hands.spec_hands.pickupDistance)
            end
        end
        local function stopHands(hands)
            if hands.carryingPlayer ~= nil and hands.carryingPlayer.targeter ~= nil then
                hands.carryingPlayer.targeter:removeTargetType(Class)
            end
            hands:detachTool()
            hands.isHeld, hands.carryingPlayer = false, nil
            if StubHandTool.heldTool == hands then StubHandTool.heldTool = nil end
        end
        function StubHandTool.hold(tool)
            if tool.spec_hands ~= nil then
                startHands(tool)
                return
            end
            local hands = tool.player.hands
            if hands ~= nil and hands.isHeld then
                stopHands(hands)
                hands.putAwayByTool = true
            end
            baseHold(tool)
        end
        function StubHandTool.drop(tool)
            if tool.spec_hands ~= nil then
                stopHands(tool)
                tool.putAwayByTool = false
                return
            end
            baseDrop(tool)
            local hands = tool.player.hands
            if hands ~= nil and hands.putAwayByTool and tool.player.currentHandTool == nil then
                hands.putAwayByTool = false
                startHands(hands)
            end
        end
    end

    function StubHandTool.hands(player, options)
        local tool = StubHandTool.makeTool(player, "hands", "spec_hands", options)
        local H = StubHandTool.HANDS
        local spec = tool.spec_hands
        spec.hasSuperStrength, spec.currentMaximumMass, spec.pickupDistance = false, H.MAXIMUM_MASS, H.PICKUP_DISTANCE
        spec.heldItemNode, spec.heldItemJointId, spec.currentHoldDistance = nil, nil, 0
        spec.kinematicNode = StubScene.newNode("handsKinematic", 0)
        spec.kinematicRotationNode = StubScene.newNode("handsKinematicRotation", spec.kinematicNode)
        for name, fn in pairs(StubHandTool.handsRegistered) do tool[name] = fn end  -- copied onto the tool, as the game does
        spec.dirtyFlag, spec.awaitingJoint = 1, false
        tool.isServer = true  -- a single player game is the server: it applies the rotation node to the joint itself
        tool.raiseDirtyFlags = function(self, flag) table.insert(StubHandTool.dirtyRaised, { tool = self, flag = flag }) end
        StubHandTool.hands_ = tool
        player.hands = tool  -- the game: Player.hands
        return tool
    end

    function StubHandTool.superStrength(tool, on)
        local H, spec = StubHandTool.HANDS, tool.spec_hands
        spec.hasSuperStrength = on
        spec.currentMaximumMass = on and H.SUPER_STRENGTH_MASS or H.MAXIMUM_MASS
        spec.pickupDistance = on and H.SUPER_STRENGTH_DISTANCE or H.PICKUP_DISTANCE
        local targeter = tool.carryingPlayer.targeter
        targeter:removeTargetType(_G.HandToolHands)
        targeter:addTargetType(_G.HandToolHands, _G.HandToolHands.TARGET_MASK, 0.5, spec.pickupDistance)
    end

    function StubHandTool.pressPickUp(tool)
        local target = tool.carryingPlayer.targeter.closestTargetsByKey[_G.HandToolHands]
        if target ~= nil and tool:getCanPickUpNode(target.node) then tool:pickUpTarget(target) end
    end

    function StubHandTool.breakJoint(tool) tool:onHeldItemJointBroken(1, 500) end
)luau";

namespace object_grab_test {

using namespace hand_tools_test;

// The game's hands in the game (kStubHandToolHands), for StartOptions::extraStubs. variant: see the header above.
inline std::string HandsStubs(const char* variant = "ok") {
    return std::string(kStubHandToolHands) + "\nStubHandTool.installHands('" + variant + "')\n";
}

// The player's hands are empty: the game's hands tool (Player.hands) is held, as _G.hands.
inline constexpr const char* kHoldHands = "_G.hands = StubHandTool.hands(foot.player); StubHandTool.hold(hands)";
inline void HoldHands(lua_State* L) { ASSERT_EQ(RunLuauChunk(L, kHoldHands, "=holdHands"), 0); }

// Lua helpers: the rays a test compares, each { x, y, z, dirX, dirY, dirZ } in the world, and what was drawn.
//   targeterRay()      what the targeter cast in the last game frame
//   headRay()          what the game's own getLookRay answers for the player: the first person camera
//   pointerRay()       the Pointer Ray the Companion Mod placed (VRCompanion:getPointerRay), nil without one
//   pulledBack(r, m)   the ray r with its origin moved back along it by m metres
//   sameRay(a, b), apart(a, b), rayText(r)
//   drawnRayEnd()      runs VRCompanion:draw() and answers the end point (x, y, z) of the line it drew for the Pointer Ray, or nil
inline constexpr const char* kObjectGrabHelpers = R"luau(
    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.pointerRay()
        local r = VRCompanion:getPointerRay()
        if r == nil then return nil end
        return { r.ox, r.oy, r.oz, r.dx, r.dy, r.dz }
    end
    function _G.pulledBack(r, m) return { r[1] - r[4] * m, r[2] - r[5] * m, r[3] - r[6] * m, r[4], r[5], r[6] } end
    function _G.sameRay(a, b)
        if a == nil or b == nil then return false end
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.apart(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
    function _G.rayText(r) return (r and r[1]) and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
    _G.drawnLines = {}
    _G.drawDebugLine = function(x1, y1, z1, r1, g1, b1, x2, y2, z2) table.insert(_G.drawnLines, { x2, y2, z2 }) end
    _G.drawDebugPoint = function() end
    function _G.drawnRayEnd()
        _G.drawnLines = {}
        VRCompanion:draw()
        local line = _G.drawnLines[#_G.drawnLines]
        if line == nil then return nil end
        return line[1], line[2], line[3]
    end
)luau";

// The mod started on a player on foot with the game's hands (handsVariant: how HandToolHands is) and the Lua helpers above. The
// player holds the hands tool unless `hold` is false. startOptions.extraStubs is a Lua chunk that runs before the hands (kXmlStubs,
// SprayCanStubs()...).
inline void StartObjectGrabMod(lua_State* L, fs25vr::VRBridge& bridge, const StartOptions& startOptions, const char* handsVariant = "ok",
                               bool hold = true) {
    const std::string stubs = std::string(startOptions.extraStubs) + "\n" + HandsStubs(handsVariant);
    StartOptions options = startOptions;
    options.extraStubs = stubs.c_str();
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
    ASSERT_EQ(RunLuauChunk(L, kObjectGrabHelpers, "=objectGrabHelpers"), 0);
    if (hold) ASSERT_NO_FATAL_FAILURE(HoldHands(L));
}

// A test of Object Grab that reads what the game reads from the Virtual Gamepad: the mod started on a player on foot with the game's
// hands, the Virtual Gamepad on and the game clock set (the in-use timeout is measured on it). The suite name is one the filter for the
// Companion Mod tests (VirtualGamepad*) covers.
class VirtualGamepadObjectGrabTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    void Start(const StartOptions& startOptions = StartOptions()) {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, startOptions));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }
    bool PadGrip(uint32_t hand) { return (Pad().wButtons & (hand == kLeft ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0; }

    // How many haptic pulses reached a hand (the fake controllers keep them all).
    int PulsesTo(uint32_t hand) const {
        int count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == static_cast<int>(hand)) ++count;
        }
        return count;
    }

    void Squeeze(uint32_t hand, float grip) { headset.SetHandInputs(hand, hand_tools_test::Squeeze(0.0f, grip)); }
    int PickUps() { return static_cast<int>(Number(L, "#StubHandTool.pickUps")); }
    int Drops() { return static_cast<int>(Number(L, "#StubHandTool.drops")); }
};

}  // namespace object_grab_test

// A Motion Controller pointing at (x, y, z) in tracking space (the index fingertip is there and the finger runs along -Z of the
// pose given): it moves a little, then holds the pose, so that it is "in use". The game clock is set by the test once.
#define POINT_HAND(hand, x, y, z) \
    do { \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPointing((hand), hand_tools_test::HandAt((x), (y), (z))); \
        FRAMES(2); \
    } while (0)
