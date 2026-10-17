-- ====================================================================
-- FS25 VR Companion Mod: Hand Tools
--
-- Puts the player's Hand Tools into the Tool Hand (ADR-0029). In first person on foot the game links a held Hand Tool to the
-- player camera's pitch node (HandTool:attachToolToCamera), so it moves with the head. While the Tool Hand is tracked, this
-- module links a SUPPORTED Hand Tool (recognised by its spec_ table: the pressure washer lance, the flashlight, the chainsaw, the spray can, the horse brush,
-- the Highlands Fishing DLC's fishing rod, ADR-0031)
-- into the Tool Hand's Virtual Hand instead; when the Tool Hand is set down or loses tracking, or the feature is off, the tool
-- is where the game puts it again. Every other Hand Tool (other mods' tools) and every other player's tool in
-- a multiplayer session is left exactly as the game has it. Terms (Hand Tool, Tool Hand): CONTEXT.md.
--
-- SECTIONS (one per concern): Settings (vrHandTools, vrToolHand) | Supported tools (SUPPORTED_TOOLS, HAND_TOOL_HANDLE_OFFSETS,
-- HAND_TOOL_POINTING_AXES) | Wraps (WRAP_SPECS) | Placement | Trigger (the tool's use, read off the pad) | The fishing rod's
-- state | Keeping the Tool Hand in use | The look ray |
-- The chainsaw (cut plane from the bar) | The wrist's roll (cut angle) | The flashlight's beam | No screen-centre overlays (the
-- spray can's, the horse brush's and the chainsaw's onDraw) | The horse brush's touch (the touching cap) | Haptics | The spray can's feedback (target tick, marker type message) | Update |
-- Diagnostics, console commands, reset.
--
-- THE WRAPS (ADR-0026, ADR-0027): a wrap passes through to the game's own function whenever the feature is off, Motion
-- Controllers are off, the tool is not the local player's, is unsupported, or is not in a tracked Tool Hand; this is re-checked
-- on every call, never cached, so that "off" takes effect at once. If a function a wrap depends on is missing or has another
-- shape when the map loads, the feature gives up for the map: every wrap is lifted, ONE warning is logged and the game's Hand
-- Tools work unchanged. A wrap is only taken off again when it is still the function this module put there.
--
-- PUBLIC SURFACE
--   VRCompanion.handToolsEnabled / setHandToolsEnabled(enabled, save)   boolean, default true, saved (`vrHandTools on|off`)
--   VRCompanion:getToolHand() -> hand / setToolHand(hand, save)   the Motion Controller that holds Hand Tools (0 left, 1 right),
--                                                       saved (`vrToolHand left|right`); a tool held now moves to the new hand at once
--   VRCompanion:saveHandToolsSetting / loadHandToolsSetting(xmlFile, xml)   settings chain (the Tool Hand rides along)
--   VRCompanion.HAND_TOOL_HANDLE_OFFSETS                per tool kind: where the tool's root node sits in the Virtual Hand's grip frame
--   VRCompanion.HAND_TOOL_POINTING_AXES                 per tool kind: the node a tool aims from and the axis it aims along
--   VRCompanion:isHandHoldingHandTool(hand) -> boolean  the hand is the Tool Hand and a supported Hand Tool is in it now
--   VRCompanion:getHeldHandTool() -> tool, kind         the Hand Tool the player holds and its supported kind (nil kind: unsupported)
--   VRCompanion:isHandKeptInUseByHandTool(hand) -> boolean  the hand's Hand Tool keeps it in use however still it is held (its
--                                                       trigger is using the tool now, or the fishing rod in it has its line out);
--                                                       Motion Controllers keep that hand in use
--   VRCompanion:getHandToolRay([player]) -> x, y, z, dirX, dirY, dirZ   the tool's pointing ray in the world while a supported
--                                                       tool is in the Tool Hand (nil otherwise: the head's ray is the game's)
--   VRCompanion:getHandToolLookRay([player]) -> x, y, z, dirX, dirY, dirZ   that ray with its origin moved back along the axis by the
--                                                       tool's reachBehind field (the spray can's and the horse brush's 0.5 m); this is what Player:getLookRay
--                                                       answers then, so what the targeter casts
--   VRCompanion:getChainsawCutState([tool]) -> "idle" | "delimbing" | "cutting" | nil   nil when the tool (default: the one held) is no chainsaw
--   VRCompanion:getFishingRodState([tool]) -> state, lineOut, bend   the fishing rod's state ("idle" | "charging" | "waiting" |
--                                                       "bite" | "reeling" | "caught" | "unknown"), whether its line is out and its
--                                                       rodBendFactor; "unknown", false, 0 when its spec cannot be read; nil when the
--                                                       tool (default: the one held) is no fishing rod. The only reader of the rod's spec
--   VRCompanion:getChainsawCutRoll(tool) -> radians|nil the wrist's roll about the bar while the chainsaw is in the tracked Tool Hand
--                                                       (nil: leave the roll to the game's roll action)
--   VRCompanion.HAND_TOOL_HAPTICS / HAND_TOOL_HAPTIC_PULSES / _INTERVAL_MS   per tool kind: what the Tool Hand feels now (a level,
--                                                       and optionally its strength), the pulses (ms, strength, optional intervalMs)
--                                                       and how often they are requested again; no entry: silent
--   VRCompanion:updateHandTools(dt)                     once per update, called by updateMotionControllers on every path
--   VRCompanion:placeHandToolsForRender()               before every rendered frame, called by placeVirtualHandsForRender
--   VRCompanion:getHandToolsDiagnostics() -> string     the state of Hand Tools, one line
--   VRCompanion:registerHandToolsConsoleCommands() / resetHandTools()   map load / unload
--   Console: vrHandTools [on|off], vrToolHand [left|right], vrHandToolsDiagnostics (also describes an unsupported held tool in
--     the log, "Hand Tool probe:" lines, ADR-0030; the one-line answer is unchanged),
--     vrHandToolOffset <tool> <nudge|turn|reset> (tuning HAND_TOOL_HANDLE_OFFSETS live; not saved)
--
-- Hooks into the rest of the mod (each a single guarded call):
--   MotionControllers  updateMotionControllers -> updateHandTools(dt) on every path (also when input is ignored or the
--                      Injector offers no hand states, so a tool goes back to the camera); the saved setting, the console
--                      commands and the reset chain through the Motion Controller functions of the same names. Pointer Hand
--                      selection never picks a hand that isHandHoldingHandTool in the world (a menu is not the world: either hand
--                      can point), and a hand that isHandKeptInUseByHandTool stays in use however still it is held
--   VirtualGamepad     updateHandTools asks takeVirtualGamepadInput(toolHand, "trigger") every update the Tool Hand holds a
--                      supported tool (the use is made here, so VirtualGamepad needs no hook); false is the held-input handover
--   VirtualHands       getVirtualHandHolder(hand) is the node a tool is linked below; a hand that isHandHoldingHandTool is
--                      drawn closed around the handle, its index finger still following the trigger
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.HandTools = VRCompanion.HandTools or {}

local engine = VRCompanion.getEngineGlobal  -- VRCompanionCore.lua, loaded before this file

local HAND_NAMES = { [0] = "left", [1] = "right" }

local function newState()
    return {
        tool = nil,          -- the Hand Tool the game last attached to the camera (checked to be held before it is used)
        cameraPlacedTool = nil,  -- the tool the game last placed through attachToolToCamera (read by the description of an unsupported tool)
        linkedTool = nil,    -- the tool this module has linked into a Virtual Hand
        linkedHand = nil,    -- ... and the hand
        linkedHolder = nil,  -- ... and the Virtual Hand's node
        reported = nil,      -- the placement last logged
        givingBack = false,  -- the tool is being handed back to the game's own placement
        trigger = {          -- the Tool Hand's trigger as a use of the tool (see "Trigger")
            tool = nil,      -- the tool the trigger is using
            entry = nil,     -- ... and its SUPPORTED_TOOLS entry
            hand = nil,      -- ... and the hand
            down = false,    -- the trigger counts as pulled (with hysteresis)
            active = false,  -- the tool's use handler was last called as held, and has not been called as released since
        },
        chainsaw = {         -- the chainsaw's cut plane (see "The chainsaw")
            tool = nil,      -- the chainsaw it is of
            yaw = nil,       -- the heading of the plane: the bar's, locked while a cut is made
        },
        haptics = {          -- the Tool Hand's buzz (see "Haptics")
            level = nil,     -- what is being felt now: a key of HAND_TOOL_HAPTIC_PULSES, nil for nothing
            hand = nil,      -- ... on which hand
            sinceMs = 0,     -- game milliseconds since the last pulse was requested
        },
        sprayCan = {         -- what the spray can's feedback has told the player (see "The spray can's feedback")
            tool = nil,      -- the can in the tracked Tool Hand
            typeIndex = nil, -- the marker type index last named
            treeNode = nil,  -- the target tree last ticked for
        },
        problem = nil,       -- why Hand Tools gave up, once it did (the warning is logged then and never again)
        toolProblems = {},   -- per tool kind: why that tool is left to the game, once it was found reshaped (warned once)
    }
end

local state = newState()

-- ---- Settings -----------------------------------------------------------------------------------------------------------

-- vrHandTools on|off: saved, default on. Hand Tools also need Motion Controllers on.
VRCompanion.DEFAULT_HAND_TOOLS_ENABLED = true
VRCompanion.handToolsEnabled = VRCompanion.DEFAULT_HAND_TOOLS_ENABLED

local SETTING_KEY_ENABLED = "vrSettings.handTools"

function VRCompanion:saveHandToolsSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.handToolsEnabled ~= false)
    end
    self:saveToolHandSetting(xmlFile, xml)
end

-- A key that is missing keeps the current value.
function VRCompanion:loadHandToolsSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.handToolsEnabled = loaded
    end
    self:loadToolHandSetting(xmlFile, xml)
end

-- ---- Settings: the Tool Hand ------------------------------------------------------------------------------
-- vrToolHand left|right: saved, default right. VRCompanion.toolHand is nil (the right hand) or a hand id; getToolHand answers it.
-- Everything the Tool Hand does follows it: the tool's place, the trigger use, the Pointer Hand rule and the held-tool pose all
-- read getToolHand() live, so changing it moves them on the next update (setToolHand also moves the tool at once).
local SETTING_KEY_TOOL_HAND = "vrSettings.toolHand"
local TOOL_HAND_BY_NAME = { left = 0, right = 1 }

function VRCompanion:saveToolHandSetting(xmlFile, xml)
    if type(xml.setString) == "function" then
        xml.setString(xmlFile, SETTING_KEY_TOOL_HAND, HAND_NAMES[self:getToolHand()])
    end
end

-- A key that is missing (or not a hand) keeps the current choice.
function VRCompanion:loadToolHandSetting(xmlFile, xml)
    if type(xml.getString) ~= "function" then return end
    local str = xml.getString(xmlFile, SETTING_KEY_TOOL_HAND)
    local hand = type(str) == "string" and TOOL_HAND_BY_NAME[string.lower(str)] or nil
    if hand ~= nil then self.toolHand = hand end
end

-- ---- Supported tools ----------------------------------------------------------------------------------------------------
-- The tools that go into the Tool Hand, recognised by the specialization table the game gives the tool (`spec`). A new
-- tool is added here, with a HAND_TOOL_HANDLE_OFFSETS entry of the same kind.
-- `useHandler` is the name of the tool's own function the game's ACTIVATE_HANDTOOL binding calls (callback(self, action,
-- inputValue)); the Tool Hand's trigger calls it too (see "Trigger"). The lance washes while the input is held.
-- `useHandlerClass` (optional) names the global specialization class to look the handler up on when the tool does not carry it
-- itself: the game registers some handlers as functions of the tool (the lance's onWashAction) and leaves others on the
-- specialization class only, called with the tool as `self` by the action event (HandToolChainsaw.onCutAction).
-- `useHandlerSpecialization` (optional) names the specialization whose class to look the handler up on when that class has no global
-- name (a DLC's: its scripts run in their own environment): the class is the tool's own, in tool.specializations next to its name
-- in tool.specializationNames.
-- `useOnce` (optional) marks a tool whose handler is called once per press and once per release, as the game's action event does
-- for it (the chainsaw's, registered for trigger up and down), not on every update the input is held (the lance's).
-- `toggle` (instead of a useHandler) makes the trigger a PRESS: each pull of the trigger calls toggle(tool) once.

-- The flashlight: a press of the trigger switches it on or off through the flashlight's own switch,
-- HandToolFlashlight:setFlashlightIsActive (the same call the game's network event makes; no `noEventSend`, so other players see
-- the light). The player keeps its own flag for the light (Player.isFlashlightActive, which the game restores the light from
-- when a Hand Tool is dropped), so that follows. UNCERTAIN until the headset: which action the game's own toggle
-- (TOGGLE_LIGHTS_FPS, the body of its handler is not in the readable source) uses; only this function changes.
local function toggleFlashlight(tool)
    local spec = tool.spec_flashlight
    local on = not (spec ~= nil and spec.isActive == true)
    tool:setFlashlightIsActive(on)
    local player = type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    if type(player) == "table" and type(player.isFlashlightActive) == "boolean" then player.isFlashlightActive = on end
end

local SUPPORTED_TOOLS = {
    { kind = "lance", label = "pressure washer lance", spec = "spec_highPressureWasherLance", useHandler = "onWashAction" },
    { kind = "flashlight", label = "flashlight", spec = "spec_flashlight", toggle = toggleFlashlight },
    -- Chainsaw
    { kind = "chainsaw", label = "chainsaw", spec = "spec_chainsaw", useHandler = "onCutAction", useHandlerClass = "HandToolChainsaw", useOnce = true },
    -- The spray can (ADR-0030). The kind is the name `vrHandToolOffset` takes. activateSpraying only sets a flag (the can's
    -- own update acts on its rising edge and clears it), so, as the lance's, it is called every update the trigger is held.
    { kind = "spraycan", label = "spray can", spec = "spec_sprayCan", useHandler = "activateSpraying" },
    -- The horse brush (ADR-0030). Its handler is held (spec.isCleaning = inputValue > 0) and lives on the specialization class only,
    -- like the chainsaw's cut handler; it is called every update the trigger is held, then once with 0 when it is let go or the brush
    -- leaves the hand, because the brush's cleaning flag changes only when the handler is called.
    { kind = "horsebrush", label = "horse brush", spec = "spec_horseBrush", useHandler = "onBrushAction", useHandlerClass = "HandToolHorseBrush" },
    -- The fishing rod (ADR-0031), from the Highlands Fishing DLC. A game without the DLC never holds one. Its ACTIVATE_HANDTOOL handler
    -- fires on press and release only (#125): held, it charges a cast, let go it throws it, and it hooks and reels the same way, so
    -- it is `useOnce` like the chainsaw's. The handler is on the DLC's specialization class, which has no global name: it is looked
    -- up among the rod's own specializations by the specialization's name (`useHandlerSpecialization`).
    { kind = "fishingrod", label = "fishing rod", spec = "spec_pdlc_highlandsFishingPack.fishingRod", useHandler = "onActivateAction",
      useHandlerSpecialization = "pdlc_highlandsFishingPack.fishingRod", useOnce = true },
}

-- WHERE EACH TOOL SITS IN THE HAND: the pose of the tool's ROOT node in the Virtual Hand's grip frame (the frame of the
-- holder node: origin at the centre of the tube the closed fingers form, -Y toward the fingertips, -Z from the little finger
-- toward the thumb, +X out of a left palm and into a right one; VRCompanionVirtualHands.lua). The one place to adjust how a
-- tool sits in the hand. The rotation maps the tool's own axes onto the hand's so that the tool points where the fingers
-- point: the tool's +Z (the lance's nozzle runs along it) onto the grip frame's -Y, the tool's +Y (up) onto -Z (toward the
-- thumb), which is the half turn about (0, 1, -1) below. Metres, a unit quaternion.
-- With that rotation, putting the grip origin on the tool's own point P = (px, py, pz) (tool axes) means the root sits at
-- (px, pz, py) in the grip frame. The handle points come from the game's own tool models (data/handTools/**.i3d, in the root's
-- axes): the chainsaw's rear handle is its `rightHandTarget` node.
-- Tuned in the headset (vrHandToolOffset) for the right hand; only this table changes. BOTH HANDS use the same entry: a
-- point of the tool is put on the grip origin of either hand.
VRCompanion.HAND_TOOL_HANDLE_OFFSETS = {
    -- The lance: the hand 4 cm further back along the lance than the root (grip at tool z = -0.04).
    lance = {
        position = { x = 0.0, y = -0.04, z = 0.0 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
    -- The flashlight lies across the fist, its lens out between the index finger and the thumb: the tool's +Z halfway between
    -- the fingertips (-Y) and the thumb (-Z) of the grip frame, a half turn about (0, 0.9239, -0.3827); 2 cm along the hand's
    -- thumb side from the root.
    flashlight = {
        position = { x = 0.0, y = 0.0, z = -0.02 },
        rotation = { x = 0.0, y = 0.9238795, z = -0.3826834, w = 0.0 },
    },
    -- Chainsaw: the saw's bar runs along the tool's +Z, as the game turns the model (setDirection) to point
    -- its +Z where the camera looks. The hand holds the rear handle (the game's model has it at (0.056, -0.042, -0.326) of
    -- the root), turned like the flashlight, the bar between the index finger and the thumb.
    chainsaw = {
        position = { x = 0.056, y = -0.276, z = -0.232 },
        rotation = { x = 0.0, y = 0.9238795, z = -0.3826834, w = 0.0 },
    },
    -- The spray can stands upright in its i3d with the nozzle along its +Z: held like the lance, the tool's +Z along the fingers
    -- and its +Y (up, the can's body) toward the thumb, the hand 5 cm up the can from the root (grip at tool y = 0.05) and 1 cm to
    -- the tool's -X side. Tuned in the headset with vrHandToolOffset spraycan (from a first guess of 10 cm up and centred).
    spraycan = {
        position = { x = -0.01, y = 0.0, z = 0.05 },
        rotation = { x = 0.0, y = 0.7071068, z = -0.7071068, w = 0.0 },
    },
    -- The horse brush's bristles (the `fibers` shape, 4 cm below the brush node) face the tool's -Y; held with the bristles along the
    -- fingers, the tool's axes the hand's (no turn), so the hand lies on the back of the brush. The hand sits where the game's own
    -- third person hold does (the tool's `handNode` at (0, -0.009, -0.117) of the root), which puts the root at the opposite of that in
    -- the grip frame. That was the first guess from the i3d; these numbers were fitted in the headset (vrHandToolOffset horsebrush).
    horsebrush = {
        position = { x = 0.03, y = 0.009, z = -0.033 },
        rotation = { x = 0.0, y = 0.0, z = -0.7071068, w = 0.7071068 },
    },
    -- The fishing rod runs along the tool's +Z from its root to its tip (UNCERTAIN, see HAND_TOOL_POINTING_AXES). Held like the
    -- flashlight, through the fist with the tip out between the index finger and the thumb, the reel below. A FIRST GUESS: the
    -- DLC's i3d is packed and the probe dump (#125) names the rod's `handNode` (the third person hold) without its position, so the
    -- hand is put on the root itself until the headset round fits it (vrHandToolOffset fishingrod).
    fishingrod = {
        position = { x = 0.0, y = 0.0, z = 0.0 },
        rotation = { x = 0.0, y = 0.9238795, z = -0.3826834, w = 0.0 },
    },
}

local function copyOffset(offset)
    return {
        position = { x = offset.position.x, y = offset.position.y, z = offset.position.z },
        rotation = { x = offset.rotation.x, y = offset.rotation.y, z = offset.rotation.z, w = offset.rotation.w },
    }
end

-- The table as shipped, for `vrHandToolOffset <tool> reset`.
local DEFAULT_HANDLE_OFFSETS = {}
for kind, offset in pairs(VRCompanion.HAND_TOOL_HANDLE_OFFSETS) do
    DEFAULT_HANDLE_OFFSETS[kind] = copyOffset(offset)
end

-- WHERE EACH TOOL POINTS (the look ray): the ray the game's targeter casts starts at `node(tool)`'s origin and runs
-- along `direction`, a unit vector in that node's own axes. One entry per tool kind, the place to adjust how a tool aims.
-- The lance washes from its raycastNode along that node's +Z axis, the nozzle. UNCERTAIN until checked in the headset:
-- only this table changes. An entry may also have a `position`, the point of the node the ray starts at (default: the node's
-- origin); `position` and `direction` are tables or functions of the tool that answer one.
-- The flashlight's beam leaves its lens the way the game's own placement (HandToolFlashlight:updateTransform) takes it out of the
-- tool model: from the point `lightNodeOffset` of the node the light node was below (`lightNodeParent`), along `lightNodeForward`
-- in that node's axes.
VRCompanion.HAND_TOOL_POINTING_AXES = {
    lance = {
        node = function(tool) return tool.spec_highPressureWasherLance.raycastNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
    },
    flashlight = {
        node = function(tool) return tool.spec_flashlight.lightNodeParent end,
        position = function(tool)
            local spec = tool.spec_flashlight
            return { x = spec.lightNodeOffsetX or 0.0, y = spec.lightNodeOffsetY or 0.0, z = spec.lightNodeOffsetZ or 0.0 }
        end,
        direction = function(tool)
            local spec = tool.spec_flashlight
            return { x = spec.lightNodeForwardX or 0.0, y = spec.lightNodeForwardY or 0.0, z = spec.lightNodeForwardZ or 1.0 }
        end,
    },
    -- Chainsaw: from the tool's root along its +Z, the bar. The root, not the model node: the game turns the
    -- model toward the camera each update and moves it along the cut while cutting, while the root stays where the hand put it.
    -- The hit point of this ray on the trunk is where the game puts the cut plane's node, so the cut height is where the bar
    -- meets the trunk. UNCERTAIN until checked in the headset.
    chainsaw = {
        node = function(tool) return tool.rootNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
    },
    -- Spray can: the can's visual node (sprayCan_vis, the game's shake node, which the game moves a few centimetres while the can
    -- sprays) along its +Z, the nozzle. UNCERTAIN until checked in the headset: the axis from the i3d, and whether the shake makes
    -- the target change mid-spray (then point from a steadier node, the tool's root, with the same axis).
    -- `reachBehind` (optional, metres): the look ray starts this far BEHIND the point the tool points from, along its axis. The
    -- game's targeter measures a tree's distance from the ray's origin and finds none nearer than HandToolSprayCan's 0.5 m, so the
    -- origin is moved back by exactly that: the can finds a trunk from right against the nozzle (0 m) to 1 m past it (the game's
    -- 1.5 m reach). A tool without the field (the lance, the flashlight, the chainsaw) has its ray unchanged.
    spraycan = {
        node = function(tool) return tool.spec_sprayCan.canNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
        reachBehind = 0.5,
    },
    -- Horse brush: the brush node (`brush`, the game's sway node: it moves a few centimetres while the brush cleans) along its -Y,
    -- toward the bristles. UNCERTAIN until checked in the headset: the axis from the i3d, and whether the sway makes the target
    -- change mid-brushing (then point from the tool's root, with the same axis). `reachBehind` is the game's 0.5 m minimum for
    -- animals; `touchReach` (metres) is how far past the bristles an animal still counts as touched (see "The horse brush").
    horsebrush = {
        node = function(tool) return tool.spec_horseBrush.brushNode end,
        direction = { x = 0.0, y = -1.0, z = 0.0 },
        reachBehind = 0.5,
        touchReach = 0.25,
    },
    -- Fishing rod: from the rod's root along its +Z, toward the tip, with no `reachBehind` (ADR-0031). The cast should land the way
    -- the rod points. UNCERTAIN until checked in the headset: the axis is a first guess (the game's tools point along +Z; the DLC's
    -- i3d is packed), and whether the DLC aims its cast with the look ray at all.
    fishingrod = {
        node = function(tool) return tool.rootNode end,
        direction = { x = 0.0, y = 0.0, z = 1.0 },
    },
}

-- The supported kind of a Hand Tool and its entry, or nil for an unsupported one.
local function supportedKind(tool)
    if type(tool) ~= "table" then return nil end
    for _, entry in ipairs(SUPPORTED_TOOLS) do
        if tool[entry.spec] ~= nil then return entry.kind, entry end
    end
    return nil
end

-- The class of the tool's specialization called `name` (the game keeps the classes in tool.specializations, in step with their
-- names in tool.specializationNames), or nil.
local function specializationClass(tool, name)
    local names, classes = tool.specializationNames, tool.specializations
    if type(names) ~= "table" or type(classes) ~= "table" then return nil end
    for index, specName in ipairs(names) do
        if specName == name then
            local class = classes[index]
            return type(class) == "table" and class or nil
        end
    end
    return nil
end

-- The function the game's ACTIVATE_HANDTOOL binding calls for the tool (SUPPORTED_TOOLS[].useHandler): the tool's own, else the one
-- on its specialization class (by global name, or among the tool's specializations). Whatever is there, function or not; nil for none.
local function useHandlerOf(tool, entry)
    local handler = tool[entry.useHandler]
    if type(handler) ~= "function" and entry.useHandlerClass ~= nil then
        local class = engine(entry.useHandlerClass)  -- a handler the game keeps on the specialization class only
        handler = type(class) == "table" and class[entry.useHandler] or nil
    end
    if type(handler) ~= "function" and entry.useHandlerSpecialization ~= nil then
        local class = specializationClass(tool, entry.useHandlerSpecialization)  -- a class with no global name
        handler = class ~= nil and class[entry.useHandler] or nil
    end
    return handler
end

-- ---- Wraps --------------------------------------------------------------------------------------------------------------
-- WRAP_SPECS is keyed by the name of the function wrapped. Each entry:
--   class        the name of the global table the function is on
--   fn           the function's name on the class, when it is not the key (two classes can have an `onUpdate`)
--   parameters   how many parameters the wrap needs the function to declare at least (a reshaped function declares fewer)
--   describe     what the parameters are, for the warning
--   make         function(original) -> the wrapper (attached by the section that owns the wrap, which needs locals defined below
--                this table); an entry without one is CHECK-ONLY: a function the module depends on but does not wrap, which is
--                only checked at map load (the feature gives up when it is missing or reshaped)
-- Every function the module depends on is listed here, so that the give-up check knows all of them.
local WRAP_SPECS = {
    attachToolToCamera = { class = "HandTool", parameters = 1, describe = "(tool)" },
    getLookRay = { class = "Player", parameters = 1, describe = "(player)" },
    -- The chainsaw: the specialization's update (the game's event dispatch reads it off the class at every call) and its roll
    -- action's handler
    chainsawOnUpdate = { class = "HandToolChainsaw", fn = "onUpdate", parameters = 2, describe = "(tool, dt)" },
    chainsawOnRoll = { class = "HandToolChainsaw", fn = "onRollAction", parameters = 3, describe = "(tool, action, inputDelta)" },
    -- The flashlight's update, which places the light node (the instance's updateTransform is a copy made when the tool was
    -- made, which a wrap of the class would not reach)
    flashlightBeam = { class = "HandToolFlashlight", fn = "onUpdate", parameters = 2, describe = "(flashlight, dt)" },
    -- The draw events of the tools that draw at the centre of the view (see "No screen-centre overlays")
    sprayCanOnDraw = { class = "HandToolSprayCan", fn = "onDraw", parameters = 1, describe = "(tool)" },
    chainsawOnDraw = { class = "HandToolChainsaw", fn = "onDraw", parameters = 1, describe = "(tool)" },
    horseBrushOnDraw = { class = "HandToolHorseBrush", fn = "onDraw", parameters = 1, describe = "(tool)" },
    -- The horse brush's animal lookup, which its update calls through the class (see "The horse brush's touch"), and its use handler,
    -- which the trigger calls (CHECK-ONLY: a game without it gives Hand Tools up, rather than the trigger doing nothing unseen)
    horseBrushLookup = { class = "HandToolHorseBrush", fn = "getHusbandryAndClusterFromNode", parameters = 2, describe = "(player, node)" },
    horseBrushOnBrushAction = { class = "HandToolHorseBrush", fn = "onBrushAction", parameters = 3, describe = "(tool, action, inputValue)" },
    -- CHECK-ONLY, never wrapped: the chainsaw's cut plane takes its heading from camera:getRotation, which updateChainsawFromTheTool
    -- answers differently for the length of the chainsaw's own update by shadowing it on the camera instance. The shadow depends
    -- on the camera having that function, so a game without it gives Hand Tools up.
    playerCameraGetRotation = { class = "PlayerCamera", fn = "getRotation", parameters = 1, describe = "(camera)" },
}

-- wraps[key] = { class = <the table wrapped>, fn = <the function's name on it>, original = <what it was>, wrapper = <what the wrap put there> }
local wraps = {}

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
local function parameterCount(fn)
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

-- Why a function is not what its wrap expects, or nil when it is.
local function shapeProblem(spec, fn)
    if fn == nil then return "it is missing" end
    if type(fn) ~= "function" then return "it is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < spec.parameters then
        return string.format("it takes %d parameters, not %s", count, spec.describe)
    end
    return nil
end

-- Wraps a function on its class once per class table. A function that is not as expected is left alone (checkWraps reports it).
local function wrapFunction(name)
    local spec = WRAP_SPECS[name]
    local class = engine(spec.class)
    if type(class) ~= "table" or spec.make == nil then return end
    local current = wraps[name]
    if current ~= nil and current.class == class then return end
    local fn = spec.fn or name
    local original = class[fn]
    if shapeProblem(spec, original) ~= nil then return end
    local wrapper = spec.make(original)
    wraps[name] = { class = class, fn = fn, original = original, wrapper = wrapper }
    class[fn] = wrapper
end

local function wrapAll()
    for name in pairs(WRAP_SPECS) do wrapFunction(name) end
end

-- Takes the wraps off again, each only when it is still the wrapper put there.
local function liftWraps()
    for name, wrap in pairs(wraps) do
        if wrap.class[wrap.fn] == wrap.wrapper then wrap.class[wrap.fn] = wrap.original end
    end
    wraps = {}
end

-- Hand Tools cannot rely on the game's functions: ONE warning, every wrap lifted, so that the game's Hand Tools are exactly
-- the game's.
local function giveUp(problem)
    state.problem = problem
    liftWraps()
    print("[FS25_VR] [Warning] Hand Tools disabled: " .. problem .. ". Hand Tools are unchanged.")
end

-- At map load: gives up (once) when a function the wraps expect is missing or reshaped. A game without the class at all has
-- no Hand Tools to move, which is not a failure.
local function checkWraps()
    if state.problem ~= nil then return state.problem end
    for name, spec in pairs(WRAP_SPECS) do
        local class = engine(spec.class)
        if type(class) == "table" then
            local current = wraps[name]
            local fn = spec.fn or name
            local problem = shapeProblem(spec, current ~= nil and current.original or class[fn])
            if problem ~= nil then
                giveUp(spec.class .. "." .. fn .. ": " .. problem)
                return state.problem
            end
        end
    end
    return nil
end

-- A TOOL WHOSE CLASS HAS NO GLOBAL NAME (the fishing rod's, a DLC's: SUPPORTED_TOOLS[].useHandlerSpecialization) cannot be checked
-- at map load: its class is only reachable from a tool. It is checked when the local player's tool would go into the Tool Hand. A
-- tool found reshaped (its use handler missing or not the shape the trigger calls) is left to the game for the rest of the map,
-- with ONE warning; the other Hand Tools are unaffected. A game without the DLC never holds one, so nothing is checked or warned.
local USE_HANDLER_SHAPE = { parameters = 3, describe = "(tool, action, inputValue)" }

-- Why the tool is left to the game, or nil when it is as expected (or is a tool whose functions checkWraps checks).
local function toolProblem(tool, entry)
    if entry.useHandlerSpecialization == nil then return nil end
    local known = state.toolProblems[entry.kind]
    if known ~= nil then return known end
    local problem = shapeProblem(USE_HANDLER_SHAPE, useHandlerOf(tool, entry))
    if problem == nil then return nil end
    problem = entry.useHandlerSpecialization .. "." .. entry.useHandler .. ": " .. problem
    state.toolProblems[entry.kind] = problem
    print("[FS25_VR] [Warning] Hand Tools: the " .. entry.label .. " is left to the game: " .. problem .. ". The " .. entry.label
        .. " is unchanged.")
    return problem
end

-- ---- Placement ----------------------------------------------------------------------------------------------------------

function VRCompanion:getToolHand()
    return self.toolHand or self.MOTION_HAND_RIGHT
end

-- The Hand Tool the game last attached, while it is still held (and nil when it was put away).
local function heldTool()
    local tool = state.tool
    if tool == nil then return nil end
    if type(tool.getIsHeld) ~= "function" or tool.rootNode == nil or not tool:getIsHeld() then
        state.tool = nil
        return nil
    end
    return tool
end

function VRCompanion:getHeldHandTool()
    local tool = heldTool()
    if tool == nil then return nil, nil end
    return tool, (supportedKind(tool))
end

-- The player who carries the tool, when that is the local player: the one whose Motion Controllers these are. The game attaches
-- the Hand Tools of the other players of a multiplayer session through the same functions, and those stay the game's (they are
-- drawn in the avatar's hand as ever); nil for them, and when the game does not say who the local player is.
local function carryingPlayer(tool)
    local player = type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    return type(player) == "table" and player or nil
end

local function localPlayer()
    local player = engine("g_localPlayer")
    if player == nil then
        local mission = engine("g_currentMission")
        player = type(mission) == "table" and mission.player or nil
    end
    return player
end

local function localCarryingPlayer(tool)
    local player = carryingPlayer(tool)
    return (player ~= nil and player == localPlayer()) and player or nil
end

-- The tool is carried by somebody else (a player of a multiplayer session): the game's, never ours.
local function isAnotherPlayersTool(tool)
    local player = carryingPlayer(tool)
    return player ~= nil and player ~= localPlayer()
end

-- Whether the Tool Hand holds the tool now: the feature is on, Motion Controllers are on, the tool is supported and held
-- by the local player in the first person view (the game keeps its own third person placement), and the Tool Hand's Virtual
-- Hand is there to hold it (the hand in use, its grip pose tracked, the head placed in the world). Answers the hand, or nil
-- and why not. Read live, never cached, by the wraps and by the update.
local function toolHandFor(self, tool)
    if self.handToolsEnabled == false then return nil, "Hand Tools are off" end
    if self.motionControllersEnabled == false then return nil, "Motion Controllers are off" end
    if state.problem ~= nil then return nil, "Hand Tools gave up (" .. state.problem .. ")" end
    if state.givingBack then return nil, "the tool is being given back to the game" end
    if tool == nil then return nil, "no Hand Tool is held" end
    local kind, entry = supportedKind(tool)
    if kind == nil then return nil, "the Hand Tool is not supported" end
    local player = localCarryingPlayer(tool)
    if player == nil then return nil, "the Hand Tool is not the local player's" end
    if player.camera == nil or player.camera.isFirstPerson ~= true then
        return nil, "not in the first person view"
    end
    local problem = toolProblem(tool, entry)
    if problem ~= nil then return nil, "the " .. entry.label .. " is left to the game (" .. problem .. ")" end
    local hand = self:getToolHand()
    local pose = self:getHandState(hand)
    if pose.tracked ~= true then return nil, "the Tool Hand is not tracked" end
    if not self:isHandInUse(hand) then return nil, "the Tool Hand is not in use" end
    if pose.gripPoseTracked ~= true then return nil, "the Tool Hand's grip pose is not tracked" end
    if not self:isHeadPlacedInTheWorld() then return nil, "the head is not placed in the world" end
    if type(self.getVirtualHandHolder) ~= "function" or self:getVirtualHandHolder(hand) == nil then
        return nil, "the Virtual Hand is not there"
    end
    return hand, nil
end

function VRCompanion:isHandHoldingHandTool(hand)
    local tool = heldTool()
    if tool == nil then return false end
    return toolHandFor(self, tool) == hand
end

local function forgetLink()
    state.linkedTool, state.linkedHand, state.linkedHolder = nil, nil, nil
end

-- Links the tool's root below the Virtual Hand's node with the tool's handle offset. Returns whether it did.
local function linkIntoHand(self, tool, hand)
    local holder = self:getVirtualHandHolder(hand)
    local kind = supportedKind(tool)
    local offset = kind ~= nil and self.HAND_TOOL_HANDLE_OFFSETS[kind] or nil
    if holder == nil or offset == nil then return false end
    local root = tool.rootNode
    engine("link")(holder, root)
    engine("setTranslation")(root, offset.position.x, offset.position.y, offset.position.z)
    engine("setQuaternion")(root, offset.rotation.x, offset.rotation.y, offset.rotation.z, offset.rotation.w)
    state.linkedTool, state.linkedHand, state.linkedHolder = tool, hand, holder
    return true
end

-- Logs where the tool is when that changes (a headset check reads it back from the log).
local function reportPlacement(tool, hand)
    local text = hand ~= nil and string.format("Hand Tools: the %s is in the %s hand", tostring(supportedKind(tool)), HAND_NAMES[hand])
        or "Hand Tools: the Hand Tool is where the game puts it"
    if text ~= state.reported then
        state.reported = text
        print("[FS25_VR] " .. text)
    end
end

-- The game's own placement again: the game's attachTool decides between the camera and the hand of the third person view.
local function returnToTheGame(tool)
    forgetLink()
    state.givingBack = true  -- the wrap passes this call through, whatever the hand looks like
    local ok, err = pcall(function()
        if type(tool.attachTool) == "function" then
            tool:attachTool()
        elseif type(tool.attachToolToCamera) == "function" then
            tool:attachToolToCamera()
        end
    end)
    state.givingBack = false
    if not ok then error(err, 0) end
end

-- The wrap of HandTool:attachToolToCamera: a supported tool goes into the Tool Hand while it holds it, anything else (and
-- everything while the hand does not) is the game's own.
WRAP_SPECS.attachToolToCamera.make = function(original)
    return function(tool, ...)
        if type(tool) == "table" and not isAnotherPlayersTool(tool) then
            state.tool = tool
            state.cameraPlacedTool = tool  -- for the description of an unsupported tool (placement path)
            local hand = toolHandFor(VRCompanion, tool)
            if hand ~= nil and linkIntoHand(VRCompanion, tool, hand) then
                reportPlacement(tool, hand)
                return
            end
        end
        return original(tool, ...)
    end
end

-- ---- Trigger ------------------------------------------------------------------------------------------------------------
-- While a supported tool is in the tracked Tool Hand, that hand's trigger uses it: the Companion Mod calls the tool's own
-- ACTIVATE_HANDTOOL handler (SUPPORTED_TOOLS[].useHandler) with the input held (1) while the trigger is pulled (on every update,
-- or once when the tool is `useOnce`, as the game's action event calls it) and with it released (0) when it is let go, and takes the trigger off the Virtual Gamepad (takeVirtualGamepadInput, ADR-0028) so that
-- whatever is bound to it does not also fire; the held-input rule applies as for every other use (a trigger already held when
-- the tool comes into the hand keeps its binding and uses nothing until it is let go). The game's own binding (X on a pad, the
-- mouse, the keyboard) still calls the same handler: nothing here replaces it. The flashlight is used by a press instead (a
-- SUPPORTED_TOOLS entry with `toggle`): one switch per pull, with no vibration, and the same held-input rule.

-- Calls fn(...) for the tool's entry; a failure is logged once per distinct error, not once per frame, and never breaks the update.
local function callGuarded(entry, what, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok and state.useError ~= tostring(err) then
        state.useError = tostring(err)
        print("[FS25_VR] [Warning] Hand Tools: the " .. entry.label .. "'s " .. what .. " failed: " .. tostring(err))
    end
end

-- Calls the tool's use handler as the game's action event does: callback(self, actionName, inputValue).
local function callUseHandler(tool, entry, value)
    local handler = useHandlerOf(tool, entry)
    if type(handler) ~= "function" then return end
    callGuarded(entry, "use handler", handler, tool, "ACTIVATE_HANDTOOL", value)
end

-- A tool with a `toggle` (the flashlight) is used by a PRESS instead: the pull calls toggle(tool) once, however long the trigger
-- stays down, and letting go calls nothing.
local function callToggle(tool, entry)
    callGuarded(entry, "switch", entry.toggle, tool)
end

-- Ends a use that is in progress (the tool is released), and forgets the trigger.
local function endTriggerUse()
    local use = state.trigger
    if use.active and use.tool ~= nil and use.entry ~= nil then callUseHandler(use.tool, use.entry, 0) end
    use.tool, use.entry, use.hand, use.down, use.active = nil, nil, nil, false, false
end

local function updateTrigger(self)
    local use = state.trigger
    local tool = heldTool()
    local kind, entry = nil, nil
    if tool ~= nil then kind, entry = supportedKind(tool) end
    local hand = nil
    if entry ~= nil and (entry.useHandler ~= nil or entry.toggle ~= nil) then hand = toolHandFor(self, tool) end
    if hand == nil then
        endTriggerUse()
        return
    end
    if use.tool ~= tool or use.hand ~= hand then
        endTriggerUse()
        use.tool, use.entry, use.hand = tool, entry, hand
    end
    local wasDown = use.down
    -- The trigger is ours unless it was already held when the tool came into the hand (the pad keeps it until it is let go).
    local mine = true
    if type(self.takeVirtualGamepadInput) == "function" then mine = self:takeVirtualGamepadInput(hand, "trigger") end
    if mine then
        local value = self:getHandState(hand).trigger or 0
        use.down = value >= (use.down and self.POINTER_TRIGGER_RELEASE or self.POINTER_TRIGGER_PRESS)
    else
        use.down = false
    end
    if entry.toggle ~= nil then
        if use.down and not wasDown then callToggle(tool, entry) end
    elseif use.down then
        if not use.active or not entry.useOnce then callUseHandler(tool, entry, 1) end
        use.active = true
    elseif use.active then
        callUseHandler(tool, entry, 0)
        use.active = false
    end
end

-- ---- The fishing rod's state --------------------------------------------------------------------------------------------
-- The DLC's scripts cannot be read (ADR-0031): the rod's state machine is known from the #125 dump only. spec.state is one of
-- spec.states, tables with no name, so a state is told by its keys: charging has angleScale, waiting for a bite biteTime, the bite
-- (the hook window) catchTime, caught bendResetRate, and idle nothing but its fishingRod back reference. The dump never named the
-- reeling state's keys, so reeling is what is left while the line is out. fishingLine.isInitialized says whether the line is out
-- (from the cast until it is in again), rodBendFactor how far a fish bends the rod (0 unless reeling, up to about 0.93).
-- This is the ONE place that reads the rod's spec: everything else asks getFishingRodState. Every read is guarded: a field a DLC
-- update renamed or reshaped, or a read that raises, makes the state "unknown", which keeps nothing in use and buzzes nothing.

local FISHING_ROD_KIND = "fishingrod"

-- The key each recognisable state carries, tried in this order: the first one spec.state has names the state.
local FISHING_ROD_STATE_KEYS = {
    { key = "angleScale", state = "charging" },
    { key = "biteTime", state = "waiting" },
    { key = "catchTime", state = "bite" },
    { key = "bendResetRate", state = "caught" },
}

-- The rod's state, whether its line is out and its bend, read off its spec table; nil when they cannot be read. May raise (a
-- reshaped table can): getFishingRodState calls it guarded.
local function readFishingRod(tool, specName)
    local spec = tool[specName]
    if type(spec) ~= "table" then return nil end
    local current, line, bend = spec.state, spec.fishingLine, spec.rodBendFactor
    if type(current) ~= "table" or type(line) ~= "table" or type(bend) ~= "number" then return nil end
    local lineOut = line.isInitialized
    if type(lineOut) ~= "boolean" then return nil end
    for _, known in ipairs(FISHING_ROD_STATE_KEYS) do
        if current[known.key] ~= nil then return known.state, lineOut, bend end
    end
    if lineOut then return "reeling", lineOut, bend end
    for key in pairs(current) do
        if key ~= "fishingRod" then return nil end  -- line in, with keys none of the dump's states had
    end
    return "idle", lineOut, bend
end

-- The fishing rod's state: "idle" | "charging" | "waiting" (the line out, no bite yet) | "bite" (the hook window) | "reeling" |
-- "caught" | "unknown" (the rod's spec cannot be read), then whether its line is out (false when unknown) and its bend factor (0
-- when unknown). nil when the tool (default: the Hand Tool the local player holds) is no fishing rod.
function VRCompanion:getFishingRodState(tool)
    if tool == nil then tool = heldTool() end
    local kind, entry = supportedKind(tool)
    if kind ~= FISHING_ROD_KIND then return nil end
    local ok, rodState, lineOut, bend = pcall(readFishingRod, tool, entry.spec)
    if not ok or rodState == nil then return "unknown", false, 0 end
    return rodState, lineOut, bend
end

-- ---- Keeping the Tool Hand in use ---------------------------------------------------------------------------------------
-- The Tool Hand does not stay "in use" just by holding a tool (it is set down after the in-use timeout like any hand), so
-- Motion Controllers asks this module whether the hand's Hand Tool keeps it in use, however still it is held: while the
-- hand's trigger is using the tool (a cut or a spray held with a still hand must not drop the tool), and while the fishing rod
-- in it has its line out (a bite comes 10 to 25 s after the cast, ADR-0031; an unknown state keeps nothing). An untracked hand
-- is never kept, so losing tracking still gives the tool back.
-- Answered from where the tool was last linked, not from toolHandFor: that asks whether the hand is in use, which is what this
-- decides (and a hand once set down has given the tool back, so it would never be kept again).

-- The hand's trigger is using its Hand Tool right now.
local function isTriggerUsing(hand)
    local use = state.trigger
    return use.active and use.hand == hand
end

-- The fishing rod in the hand's Virtual Hand has its line out.
local function isLineOutInHand(self, hand)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool or state.linkedHand ~= hand then return false end
    local _, lineOut = self:getFishingRodState(tool)
    return lineOut == true
end

function VRCompanion:isHandKeptInUseByHandTool(hand)
    if self:getHandState(hand).tracked ~= true then return false end
    return isTriggerUsing(hand) or isLineOutInHand(self, hand)
end

-- ---- The look ray -----------------------------------------------------------------------------------------

-- Where the tool points now: the world position of its pointing node and the world direction of its pointing axis, then the
-- HAND_TOOL_POINTING_AXES entry they came from; nil when the tool has no pointing axis or the node is not there.
local function toolPointingRay(self, tool)
    local kind = supportedKind(tool)
    local axis = kind ~= nil and self.HAND_TOOL_POINTING_AXES[kind] or nil
    if axis == nil then return nil end
    local node = axis.node(tool)
    if node == nil then return nil end
    local function resolve(value) if type(value) == "function" then return value(tool) end return value end
    local direction, position = resolve(axis.direction), resolve(axis.position)
    local x, y, z
    if position ~= nil then
        x, y, z = engine("localToWorld")(node, position.x, position.y, position.z)
    else
        x, y, z = engine("getWorldTranslation")(node)
    end
    local dx, dy, dz = engine("localDirectionToWorld")(node, direction.x, direction.y, direction.z)
    if type(x) ~= "number" or type(dx) ~= "number" then return nil end
    return x, y, z, dx, dy, dz, axis
end

-- The pointing ray of the tool in the Tool Hand and the HAND_TOOL_POINTING_AXES entry it comes from (the six numbers, then the
-- entry), or nil: the one place that decides whether there is a ray for `player` (getHandToolRay, getHandToolLookRay).
local function heldToolPointingRay(self, player)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool then return nil end
    if player ~= nil and tool:getCarryingPlayer() ~= player then return nil end
    if toolHandFor(self, tool) == nil then return nil end
    local ok, x, y, z, dx, dy, dz, axis = pcall(toolPointingRay, self, tool)
    if not ok then return nil end
    return x, y, z, dx, dy, dz, axis
end

-- The ray the player aims with while a supported Hand Tool is in the Tool Hand: x, y, z, dirX, dirY, dirZ in the world, or
-- nil (the head's ray is the game's). `player` limits it to the player who holds the tool (nil: whoever holds it). The tool
-- must be where this module put it, so that the ray starts where the tool is drawn. Read live by the Player:getLookRay wrap.
function VRCompanion:getHandToolRay(player)
    local x, y, z, dx, dy, dz = heldToolPointingRay(self, player)
    return x, y, z, dx, dy, dz
end

-- The ray the game's targeter casts for the tool in the Tool Hand: the tool's pointing ray (getHandToolRay) with its origin moved
-- back along the axis by the entry's `reachBehind` (HAND_TOOL_POINTING_AXES), or nil like getHandToolRay.
function VRCompanion:getHandToolLookRay(player)
    local x, y, z, dx, dy, dz, axis = heldToolPointingRay(self, player)
    if x == nil then return nil end
    local behind = tonumber(axis.reachBehind) or 0
    return x - dx * behind, y - dy * behind, z - dz * behind, dx, dy, dz
end

-- The wrap of Player:getLookRay, the single source PlayerTargeter:update reads (and everything that highlights by look): the
-- tool's ray while a supported Hand Tool is in a tracked Tool Hand, the game's own (the head's) otherwise.
WRAP_SPECS.getLookRay.make = function(original)
    return function(player, ...)
        if type(player) == "table" then
            local x, y, z, dx, dy, dz = VRCompanion:getHandToolLookRay(player)
            if x ~= nil then return x, y, z, dx, dy, dz end
        end
        return original(player, ...)
    end
end

-- ---- The chainsaw -------------------------------------------------------------------------------------------
-- The chainsaw cuts where its bar is. In the Tool Hand it is placed like the other tools (SUPPORTED_TOOLS, the handle offset),
-- the look ray is its bar (HAND_TOOL_POINTING_AXES), so the game's targeter finds the trunk the bar is against and the game puts
-- the node the cut plane hangs from (cameraRotationNode) at the hit point: the cut height is where the bar meets the trunk, and
-- the game's ring marker shows it. What the game still takes from the CAMERA, and this section takes from the tool:
--   * the plane's heading: HandToolChainsaw:updateSplitPlane turns cameraRotationNode to the camera's yaw (camera:getRotation).
--     The wrap of HandToolChainsaw.onUpdate gives it the bar's yaw instead, while the game's own update runs, so that everything
--     the update works out (the plane the trunk is tested against, whether a cut is valid, the plane a finished cut slices along)
--     is of the bar's plane and not of one that is turned afterwards;
--   * the model: the update turns the model (graphicalNode) to face the camera; afterwards it is put back as the i3d has it,
--     which is pointing along the tool's root, i.e. along the hand.
-- A cut keeps the heading it started with (the game does not move the plane while it cuts, and takes the player's look away
-- while it does; here the heading is locked), so moving the hand does not move the plane. The cut ends as in the game when the
-- bar is taken off the trunk: updateCutting stops it once the targeter's last ray (now the bar) starts too far from the cut.
-- Holding the trigger cuts through the chainsaw's own cut handler (HandToolChainsaw.onCutAction: called when the trigger is
-- pulled and when it is let go, as the game's action event does, "useOnce" above).
-- The cut roll (the plane's tilt about the bar) is the wrist's (see "The wrist's roll"): getChainsawCutRoll is
-- asked before each update that is not cutting; a cut keeps the roll it began with.
--
-- THE CHAINSAW'S STATE for the haptics: getChainsawCutState(tool) answers "idle", "delimbing" (the motor is
-- revved: the trigger is held without a cut) or "cutting".

-- The chainsaw's state: "idle", "delimbing" (revving without a cut) or "cutting"; nil when the tool is not a chainsaw (the
-- Hand Tool the player holds when no tool is given).
function VRCompanion:getChainsawCutState(tool)
    if tool == nil then tool = heldTool() end
    if type(tool) ~= "table" or tool.spec_chainsaw == nil then return nil end
    local states = engine("ChainsawCutState")
    if type(states) ~= "table" then return nil end
    local current = tool.spec_chainsaw.currentCutState
    if current == states.CUTTING then return "cutting" end
    if current == states.DELIMBING then return "delimbing" end
    return "idle"
end

-- Whether the chainsaw is the tool this module has put in the Tool Hand, which is the only time the wrap changes anything.
local function chainsawInTheHand(tool)
    if type(tool) ~= "table" or tool.spec_chainsaw == nil or state.linkedTool ~= tool then return false end
    return toolHandFor(VRCompanion, tool) ~= nil
end

-- ---- The wrist's roll sets the cut angle ---------------------------------------------------------------------
-- While the chainsaw is in a tracked Tool Hand, the tilt of the cut plane about the bar is how far the wrist is rolled about the
-- bar: the angle, turning right-handed about the bar's direction, from the way up that is square to the bar (the world's up, with
-- the part along the bar taken off) to the tool's own up (its +Y, the thumb side of the closed hand: HAND_TOOL_HANDLE_OFFSETS).
-- Zero is the wrist level, thumb up; the game's own clamp to +-90 degrees is applied by the update (below). A bar that points
-- straight up or down has no such roll: the plane keeps the roll it had.
-- The game's roll action (AXIS_ROTATE_HANDTOOL, HandToolChainsaw.onRollAction) does nothing meanwhile, so that a stick does not
-- fight the wrist: its wrap hands the action back to the game whenever the chainsaw is not in the tracked Tool Hand, and the
-- update empties the input a route past the wrap might have left (rollInput). With the hand untracked, set down, or Hand Tools
-- off, the game's roll action works as in the game, from the roll the wrist last left.
-- UNCERTAIN until the headset: the direction the plane turns for a wrist roll one way (the roll is taken right-handed about
-- the bar, as rotateAboutLocalAxis takes the game's own roll).

-- The wrist's roll about the bar in radians, or nil when the bar has no roll (it points straight up or down) or has no pose.
local function wristRoll(tool)
    local ok, x, _, _, dx, dy, dz = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil then return nil end
    local ux, uy, uz = -dy * dx, 1 - dy * dy, -dy * dz  -- the world's up with the part along the bar taken off
    local length = math.sqrt(ux * ux + uy * uy + uz * uz)
    if length < 1e-3 then return nil end
    ux, uy, uz = ux / length, uy / length, uz / length
    local tx, ty, tz = engine("localDirectionToWorld")(tool.rootNode, 0, 1, 0)
    if type(tx) ~= "number" then return nil end
    local sine = (uy * tz - uz * ty) * dx + (uz * tx - ux * tz) * dy + (ux * ty - uy * tx) * dz
    return math.atan2(sine, ux * tx + uy * ty + uz * tz)
end

-- The cut roll the chainsaw should have now, in radians (about its bar; the game clamps it to +-pi/2), or nil to leave it as it
-- is (the game's roll action, when the chainsaw is not in the tracked Tool Hand). Asked before each update that is not cutting; a
-- cut keeps the roll it began with.
function VRCompanion:getChainsawCutRoll(tool)
    if not chainsawInTheHand(tool) then return nil end
    return wristRoll(tool)
end

-- The heading of the bar: the yaw the game's own alignment would take from the camera (MathUtil.directionToPitchYaw: the
-- angle about the vertical of the direction), or nil when the bar points straight up or down.
local function barYaw(tool)
    local ok, x, _, _, dx, _, dz = pcall(toolPointingRay, VRCompanion, tool)
    if not ok or x == nil or dx * dx + dz * dz < 1e-6 then return nil end
    return math.atan2(dx, dz)
end

-- The chainsaw's update with the bar's heading in place of the camera's, and the model back along the tool afterwards.
local function updateChainsawFromTheTool(original, tool, dt, ...)
    local spec = tool.spec_chainsaw
    local states = engine("ChainsawCutState")
    local cutting = type(states) == "table" and spec.currentCutState == states.CUTTING
    local chainsaw = state.chainsaw
    if chainsaw.tool ~= tool then chainsaw.tool, chainsaw.yaw = tool, nil end
    if spec.rollInput ~= nil then spec.rollInput = 0 end  -- the wrist rolls the plane, not the roll action

    if not cutting then
        chainsaw.yaw = barYaw(tool) or chainsaw.yaw  -- a cut keeps the last one
        local roll = VRCompanion:getChainsawCutRoll(tool)
        if type(roll) == "number" then spec.currentCutRoll = math.max(-math.pi * 0.5, math.min(math.pi * 0.5, roll)) end
    end

    local player = tool:getCarryingPlayer()
    local camera = player ~= nil and player.camera or nil
    local yaw = chainsaw.yaw
    if yaw ~= nil and type(camera) == "table" and type(camera.getRotation) == "function" then
        -- The game reads the heading of the plane off the camera: for the length of its update the camera answers the bar's.
        local own = rawget(camera, "getRotation")  -- nil when the camera has it from its class
        local gameGetRotation = camera.getRotation
        camera.getRotation = function(self, ...)
            local pitch, _, roll = gameGetRotation(self, ...)
            return pitch, yaw, roll
        end
        local ok, err = pcall(original, tool, dt, ...)
        camera.getRotation = own
        if not ok then error(err, 0) end
    else
        original(tool, dt, ...)  -- no heading to align from (a bar pointing straight up or down has none): the game's
    end

    -- The update turned the model to face the camera unless it is cutting (it then rides the cut guide): back along the tool.
    if tool.graphicalNode ~= nil and type(states) == "table" and spec.currentCutState ~= states.CUTTING then
        engine("setRotation")(tool.graphicalNode, 0, 0, 0)
    end
end

WRAP_SPECS.chainsawOnUpdate.make = function(original)
    return function(tool, ...)
        if chainsawInTheHand(tool) then return updateChainsawFromTheTool(original, tool, ...) end
        return original(tool, ...)
    end
end

-- The wrap of the roll action's handler: nothing happens while the chainsaw is in the tracked Tool Hand.
WRAP_SPECS.chainsawOnRoll.make = function(original)
    return function(tool, ...)
        if chainsawInTheHand(tool) then return end
        return original(tool, ...)
    end
end

-- ---- The flashlight's beam ----------------------------------------------------------------------------------
-- The game places the flashlight's light node itself each update (HandToolFlashlight:onUpdate -> updateTransform: at the camera,
-- or out of the tool model while the tool is held). While the flashlight is in the Tool Hand, the beam is placed from the lens
-- instead: at the point the look ray starts and along the way it points (HAND_TOOL_POINTING_AXES.flashlight), with the game's
-- own call, setDirection. What is wrapped is the event the game raises each update, onUpdate (looked up on the class each time
-- it is raised): the instance's updateTransform is a copy made when the tool was made, which a wrap of the class would not reach.
-- The game's placement runs first and this one after it, so the game's own is what remains whenever the tool is not in the hand.
-- A frame that is drawn without a game update (the second eye of an alternate frame render) gets the light placed again from
-- the render-time hook (placeHandToolsForRender, below), so the beam rides along with the hand as the tool does.
local function placeFlashlightBeam(tool)
    local spec = type(tool) == "table" and tool.spec_flashlight or nil
    if spec == nil or spec.lightNode == nil or heldTool() ~= tool then return end
    local x, y, z, dx, dy, dz = VRCompanion:getHandToolRay()
    if x == nil then return end
    engine("setWorldTranslation")(spec.lightNode, x, y, z)
    engine("setDirection")(spec.lightNode, dx, dy, dz, 0, 1, 0)
end

WRAP_SPECS.flashlightBeam.make = function(original)
    return function(tool, ...)
        local result = original(tool, ...)
        pcall(placeFlashlightBeam, tool)
        return result
    end
end

-- Called by the Virtual Hands after they are placed for a rendered frame (placeVirtualHandsForRender), which may have no game
-- update before it: the light node is placed from the flashlight again, so the beam rides with the hand as the tool does.
function VRCompanion:placeHandToolsForRender()
    local tool = state.linkedTool
    if tool ~= nil and tool.spec_flashlight ~= nil then pcall(placeFlashlightBeam, tool) end
end

-- ---- No screen-centre overlays ------------------------------------------------------------------------------
-- Three Hand Tools draw feedback at the centre of the view, which is head-locked: the horse brush's crosshair or brush icon, the spray can's marker type icon (and a tree icon
-- while a tree is found) and the idle chainsaw's crosshair. With the tool in the hand that reads as "aim where you look", which
-- is no longer true. While the tool is the local player's, supported and in a tracked Tool Hand (the same test as for the other
-- wraps), the wrap of its onDraw draws nothing; otherwise it is the game's own function. The game's event dispatch reads onDraw
-- off the specialization class at every call, so the wrap is on the class (as the chainsaw's update). The overlay is back as the
-- game draws it when the hand is set down or loses tracking, or the feature is off. Another tool and another player's tool are
-- never in the hand, so they are drawn by the game. The fishing rod's onDraw is NOT wrapped (ADR-0031): its charge meter, waiting
-- hint and "Hook fish" prompt are its only display of the charge and the hook window, and they are no aim crosshair.

-- Whether the tool's centre overlays are held back now: it is in the Tool Hand this module put it in.
local function drawIsHeldBack(tool)
    if type(tool) ~= "table" or state.linkedTool ~= tool then return false end
    return toolHandFor(VRCompanion, tool) ~= nil
end

local function makeDrawWrap(original)
    return function(tool, ...)
        if drawIsHeldBack(tool) then return end
        return original(tool, ...)
    end
end

WRAP_SPECS.sprayCanOnDraw.make = makeDrawWrap
WRAP_SPECS.chainsawOnDraw.make = makeDrawWrap
WRAP_SPECS.horseBrushOnDraw.make = makeDrawWrap

-- ---- The horse brush's touch ----------------------------------------------------------------------------------------
-- The brush's look ray starts `reachBehind` (the game's 0.5 m minimum for animals) behind its bristles, so that the game's targeter
-- finds an animal the bristles touch; the game's reach for animals is 3 m, so a brush in the hand would also clean a horse across the
-- stall. The brush only reaches an animal within `touchReach` (about a quarter metre) of its bristles: past that, the animal counts as
-- none. The brush's update asks the class's animal lookup (HandToolHorseBrush.getHusbandryAndClusterFromNode) which animal the targeter's
-- node is, and everything it does follows that answer: the input help's "Clean <name>", the cleaning flag, the sound, the sway and the
-- clean events. The wrap of the lookup answers no animal when the targeter's hit for the brush's target type is further than that from
-- the moved ray's origin; otherwise it is the game's own. Only the local player's brush, in the tracked Tool Hand, is capped.

-- The furthest the targeter's distance (from the moved ray's origin) can be for the animal to count: the way back to the bristles
-- and the touch past them. nil for a tool that has no touch limit.
local function touchLimit(kind)
    local axis = kind ~= nil and VRCompanion.HAND_TOOL_POINTING_AXES[kind] or nil
    if axis == nil or tonumber(axis.touchReach) == nil then return nil end
    return (tonumber(axis.reachBehind) or 0) + axis.touchReach
end

-- The distance the targeter reports for the closest animal it found for the horse brush `player` carries (from the moved ray's
-- origin), or nil when it found none or `player` has no targeter.
local function brushTargetDistance(player)
    local targeter = type(player) == "table" and player.targeter or nil
    local closest = type(targeter) == "table" and type(targeter.closestTargetsByKey) == "table"
        and targeter.closestTargetsByKey[engine("HandToolHorseBrush")] or nil
    return type(closest) == "table" and tonumber(closest.distance) or nil
end

-- Whether the animal the targeter found for the brush `player` carries is out of touch now.
local function animalIsOutOfTouch(player)
    local tool = heldTool()
    if tool == nil or state.linkedTool ~= tool then return false end
    local kind = supportedKind(tool)
    if kind ~= "horsebrush" then return false end
    if type(player) ~= "table" or tool:getCarryingPlayer() ~= player or toolHandFor(VRCompanion, tool) == nil then return false end
    local limit, distance = touchLimit(kind), brushTargetDistance(player)
    return limit ~= nil and distance ~= nil and distance > limit
end

WRAP_SPECS.horseBrushLookup.make = function(original)
    return function(player, ...)
        local ok, outOfTouch = pcall(animalIsOutOfTouch, player)  -- a failure to tell leaves the game's answer
        if ok and outOfTouch then return nil, nil end
        return original(player, ...)
    end
end

-- The supported kinds whose onDraw is wrapped (the diagnostics' "held back" is about these).
local DRAW_HELD_BACK_KINDS = { spraycan = true, chainsaw = true, horsebrush = true }

-- ---- Haptics --------------------------------------------------------------------------------------------------
-- The Tool Hand buzzes while its tool works, through VRCompanion:pulseHand (VRBridge.pulseHaptic): a light buzz for as long as
-- the chainsaw's trigger is held (delimbing, without a cut), a stronger one while it cuts, a steady
-- one while the lance sprays, a lighter one while the spray can sprays, the lightest while the horse brush cleans an animal, a
-- sharp pulse repeated through the fishing rod's hook window when a fish bites and a buzz that grows with the rod's bend while it
-- reels (ADR-0031), and nothing for the flashlight (a tool with no entry in HAND_TOOL_HAPTICS). Only the Tool Hand is pulsed, and only
-- while the tool is in it (toolHandFor: the feature on, Motion Controllers on, the hand tracked), so an untracked or set-down
-- Tool Hand, `vrHandTools off` and a tool put away all end the buzz. A pulse is one burst, so the buzz is a pulse requested again
-- every HAND_TOOL_HAPTIC_INTERVAL_MS (and at once when what is felt changes), each lasting a little longer than that so the hand
-- never goes quiet in between; a pulse with its own intervalMs (the bite) is repeated that much apart instead, with silence in
-- between. The Companion Mod's pulses win on a hand over the game's rumble (ADR-0028): that rule is the
-- Injector's and applies to these pulses like to every other.

-- What is felt: milliseconds a pulse lasts, its strength (0..1) and, for a pulse that is not a buzz, how far apart it is repeated
-- (intervalMs; default HAND_TOOL_HAPTIC_INTERVAL_MS). A level function may answer its own strength instead (the reeling's).
VRCompanion.HAND_TOOL_HAPTIC_PULSES = {
    running = { ms = 70, strength = 0.2 },   -- the chainsaw's motor revved with the trigger held, no cut yet
    cutting = { ms = 70, strength = 0.6 },   -- the chainsaw cutting
    spraying = { ms = 70, strength = 0.35 }, -- the lance's pressure
    misting = { ms = 70, strength = 0.2 },   -- the spray can's mist, lighter than the lance
    brushing = { ms = 70, strength = 0.15 }, -- the horse brush on an animal, the lightest of them
    biting = { ms = 40, strength = 0.9, intervalMs = 150 },  -- a fish on the rod's hook: sharp, apart, through the hook window
    reeling = { ms = 70, strength = 0.8 },   -- the fish pulling while the rod reels: this strength at full bend, less as it bends less
}
VRCompanion.HAND_TOOL_HAPTIC_INTERVAL_MS = 50
-- The single short pulse that is not a buzz: the spray can's tick when its nozzle comes onto a tree (see "The spray can's feedback").
VRCompanion.HAND_TOOL_TICK_PULSE = { ms = 30, strength = 0.4 }

-- What each supported tool makes felt now: function(self, tool) -> a key of HAND_TOOL_HAPTIC_PULSES, or nil; optionally also a
-- strength (0..1) for this pulse in place of the level's own (0 or less: nothing is felt). A kind with no entry
-- (the flashlight) is silent. The lance sprays while its own flag is set (HandToolHPWLance's spec.isActivated).
VRCompanion.HAND_TOOL_HAPTICS = {
    chainsaw = function(self, tool)
        local cutState = self:getChainsawCutState(tool)  -- nil: no chainsaw
        if cutState == nil then return nil end
        if cutState == "idle" then return nil end  -- the motor runs only while the trigger is held, like the lance's spray
        return cutState == "cutting" and "cutting" or "running"  -- delimbing (trigger held, no cut): the light buzz
    end,
    lance = function(self, tool)
        local spec = tool.spec_highPressureWasherLance
        return (spec ~= nil and spec.isActivated == true) and "spraying" or nil
    end,
    -- The spray can mists while its own flag is set (spec.isSpraying: set when a marker is made, cleared by the can's timer), so
    -- an empty can or a forbidden tree, which make no marker, are silent, and so is a can that is only held.
    spraycan = function(self, tool)
        local spec = tool.spec_sprayCan
        return (spec ~= nil and spec.isSpraying == true) and "misting" or nil
    end,
    -- The horse brush buzzes while it cleans an animal: the game's own state for that, the animal its update targeted while the
    -- cleaning flag is set (spec.targetedHusbandryId, which the update sets only for both and clears otherwise), so a brush that is
    -- only held, over air, or over an animal out of touch (the lookup says none) is silent.
    horsebrush = function(self, tool)
        local spec = tool.spec_horseBrush
        return (spec ~= nil and spec.targetedHusbandryId ~= nil) and "brushing" or nil
    end,
    -- The fishing rod, from its state reader (the only reader of its spec): the bite's pulse through the hook window, and while
    -- it reels a buzz as strong as the rod is bent. Idle, charging, waiting, caught and an unknown state are silent.
    fishingrod = function(self, tool)
        local rodState, _, bend = self:getFishingRodState(tool)
        if rodState == "bite" then return "biting" end
        if rodState == "reeling" then
            local full = self.HAND_TOOL_HAPTIC_PULSES.reeling.strength
            return "reeling", full * math.max(0, math.min(1, tonumber(bend) or 0))
        end
        return nil
    end,
}

local function updateHaptics(self, dt)
    local haptics = state.haptics
    local tool = heldTool()
    local hand = tool ~= nil and toolHandFor(self, tool) or nil
    local kind = hand ~= nil and supportedKind(tool) or nil
    local felt = kind ~= nil and self.HAND_TOOL_HAPTICS[kind] or nil
    local level, strength = nil, nil
    if felt ~= nil then
        local ok, answer, answeredStrength = pcall(felt, self, tool)
        if ok then level, strength = answer, tonumber(answeredStrength) end
    end
    local pulse = level ~= nil and self.HAND_TOOL_HAPTIC_PULSES[level] or nil
    strength = math.min(1, strength or (pulse ~= nil and pulse.strength) or 0)
    if pulse == nil or strength <= 0 then
        haptics.level, haptics.hand, haptics.sinceMs = nil, nil, 0
        return
    end
    haptics.sinceMs = haptics.sinceMs + (tonumber(dt) or 0)
    local interval = tonumber(pulse.intervalMs) or self.HAND_TOOL_HAPTIC_INTERVAL_MS
    if haptics.level ~= level or haptics.hand ~= hand or haptics.sinceMs >= interval then
        haptics.level, haptics.hand, haptics.sinceMs = level, hand, 0
        self:pulseHand(hand, pulse.ms, strength)
    end
end

-- ---- The spray can's feedback ---------------------------------------------------------------------------------------------
-- With the spray can's icons not drawn at the centre of the view (above), the can tells the player what they showed another way:
--   * a tick, one short light pulse on the Tool Hand, when the can's found target tree (spec.foundTargetTree, from the game's
--     targeter, which casts the nozzle's ray) changes from none to a tree or to another tree; none while it stays on the same one;
--   * a short blinking message naming the marker type when the can comes into a tracked Tool Hand and each time the type changes
--     while it is there (a type changed out of the hand is named when it comes back).
-- Untracked, set down, Hand Tools off and any other tool: nothing (the game's icons are drawn then). The buzz while the can
-- sprays is the per-kind haptics above.

-- A short blinking message on screen, when the mission can show one (a console command's confirmation, a marker type's name).
local function showBlinkingWarning(text)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function() mission:showBlinkingWarning(text, 2500) end)
    end
end

-- The text of an l10n key (the game's or this mod's modDesc.xml l10n), nil when there is none or the game has no i18n.
local function l10nText(key)
    local i18n = engine("g_i18n")
    if type(key) ~= "string" or key == "" or type(i18n) ~= "table" then return nil end
    local ok, text = pcall(function()
        if i18n:hasText(key) then return i18n:getText(key) end
        return nil
    end)
    if ok and type(text) == "string" and text ~= "" then return text end
    return nil
end

-- What the player reads for a marker type. The game defines the types in data/maps/maps_treeMarkerTypes.xml, each with a name
-- (CROSS) and a title that is an l10n key ("$l10n_treeMarker_cross"). What of that the native type keeps, and in what form, is
-- not in the readable Lua (only iconFilename is), so this takes it in the order the XML gives it: the title's text (the title
-- as it is when it is not a key), else the text under the name's key (treeMarker_<name>), else the name, else the type's
-- index. A "$l10n_" key is never shown as it is.
local function markerTypeName(spec)
    local markerType = spec.treeMarkerType
    if type(markerType) == "table" then
        local title = markerType.title
        if type(title) == "string" and title ~= "" then
            local key = string.match(title, "^%$l10n_(.+)$")
            local text = l10nText(key or title)
            if text ~= nil then return text end
            if key == nil then return title end
        end
        local name = markerType.name
        if type(name) == "string" and name ~= "" then
            return l10nText("treeMarker_" .. string.lower(name)) or name
        end
    end
    return tostring(spec.treeMarkerTypeIndex)
end

-- The message that names the type: this mod's text (MARKER_TYPE_TEXT_KEY, "%s" for the name), English when it has none.
local MARKER_TYPE_TEXT_KEY = "vrHandTools_markerType"
local function markerTypeMessage(spec)
    local name = markerTypeName(spec)
    local ok, message = pcall(string.format, l10nText(MARKER_TYPE_TEXT_KEY) or "Marker type: %s", name)
    if ok then return message end
    return "Marker type: " .. name
end

local function updateSprayCanFeedback(self)
    local feedback = state.sprayCan
    local tool = heldTool()
    local hand = tool ~= nil and toolHandFor(self, tool) or nil
    local spec = (hand ~= nil and supportedKind(tool) == "spraycan") and tool.spec_sprayCan or nil
    if spec == nil then
        feedback.tool, feedback.typeIndex, feedback.treeNode = nil, nil, nil  -- out of the hand: named and ticked afresh on return
        return
    end
    if feedback.tool ~= tool then feedback.tool, feedback.typeIndex, feedback.treeNode = tool, nil, nil end

    if feedback.typeIndex ~= spec.treeMarkerTypeIndex then
        feedback.typeIndex = spec.treeMarkerTypeIndex
        showBlinkingWarning(markerTypeMessage(spec))
    end

    local found = spec.foundTargetTree
    local node = type(found) == "table" and found.node or nil
    if node ~= nil and node ~= feedback.treeNode then
        local tick = self.HAND_TOOL_TICK_PULSE
        self:pulseHand(hand, tick.ms, tick.strength)
    end
    feedback.treeNode = node
end

-- ---- Update -------------------------------------------------------------------------------------------------------------

-- Brings the held tool to where it belongs now: into the Tool Hand, or back where the game puts it. Cheap when nothing changed.
local function syncPlacement(self)
    local tool = heldTool()
    if tool == nil then
        forgetLink()
        return
    end
    local hand = toolHandFor(self, tool)
    if hand ~= nil then
        local holder = self:getVirtualHandHolder(hand)
        local placed = state.linkedTool == tool and state.linkedHand == hand and state.linkedHolder == holder
            and engine("getParent")(tool.rootNode) == holder
        if not placed and linkIntoHand(self, tool, hand) then reportPlacement(tool, hand) end
    elseif state.linkedTool == tool then
        returnToTheGame(tool)
        reportPlacement(tool, nil)
    end
end

-- Once per update, after the hands are polled and before the Virtual Hands are placed (every path of the update).
function VRCompanion:updateHandTools(dt)
    syncPlacement(self)
    updateTrigger(self)
    updateHaptics(self, dt)  -- the trigger use above is what starts a cut or a spray
    updateSprayCanFeedback(self)  -- last: its tick is a single pulse on top of the buzz
end

-- ---- Settings command, diagnostics, console commands, reset ---------------------------------------------------------------

function VRCompanion:setHandToolsEnabled(enabled, save)
    self.handToolsEnabled = (enabled ~= false)
    syncPlacement(self)  -- at once: the tool moves between the hand and the camera without waiting for the next update
    if not self.handToolsEnabled then endTriggerUse() end  -- and a spray in progress ends
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:consoleCommandSetHandTools(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Hand Tools are %s. Usage: vrHandTools <on|off> (saved)", self.handToolsEnabled ~= false and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrHandTools <on|off>"
    end
    self:setHandToolsEnabled(enabled, true)
    showBlinkingWarning(string.format("VR Hand Tools: %s (Saved)", enabled and "on" or "off"))
    print(string.format("[FS25_VR] Hand Tools set to: %s", enabled and "on" or "off"))
    return string.format("Hand Tools set to: %s (Saved)", enabled and "on" or "off")
end

-- Chooses the Tool Hand (0 left, 1 right). A tool held now moves to the other hand at once (and a use in progress ends: the
-- other hand's trigger starts a new one); `save` writes the choice to the settings file.
function VRCompanion:setToolHand(hand, save)
    if hand ~= self.MOTION_HAND_LEFT and hand ~= self.MOTION_HAND_RIGHT then return end
    local changed = hand ~= self:getToolHand()
    self.toolHand = hand
    if changed then
        syncPlacement(self)
        endTriggerUse()
    end
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:consoleCommandSetToolHand(arg)
    local hand = TOOL_HAND_BY_NAME[string.lower(tostring(arg or ""))]
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("The Tool Hand is the %s hand. Usage: vrToolHand <left|right> (saved)", HAND_NAMES[self:getToolHand()])
    end
    if hand == nil then
        return "Usage: vrToolHand <left|right>"
    end
    self:setToolHand(hand, true)
    showBlinkingWarning(string.format("VR Tool Hand: %s (Saved)", HAND_NAMES[hand]))
    print(string.format("[FS25_VR] Tool Hand set to: %s", HAND_NAMES[hand]))
    return string.format("Tool Hand set to: %s (Saved)", HAND_NAMES[hand])
end

-- Where the game's look ray starts now, for the diagnostics: the player's own getLookRay.
local function describeLookRay(tool)
    local player = tool ~= nil and type(tool.getCarryingPlayer) == "function" and tool:getCarryingPlayer() or nil
    player = player or (g_currentMission or _G.g_currentMission or {}).player
    if player == nil or type(player.getLookRay) ~= "function" then return "look ray: unknown (no player)" end
    local ok, x, y, z = pcall(player.getLookRay, player)
    if not ok or type(x) ~= "number" then return "look ray: unknown (getLookRay failed)" end
    local fromTheTool = VRCompanion:getHandToolRay(player) ~= nil
    return string.format("look ray starts at (%.2f, %.2f, %.2f), from the %s", x, y, z, fromTheTool and "tool" or "head")
end

-- Whether the horse brush's update found an animal it may clean now (the lookup's answer, after the touching cap), and how far it is:
-- from the bristles while the brush is in the Tool Hand (the targeter measures from the ray's origin, `reachBehind` further back),
-- from the head's ray's origin otherwise.
local function describeBrushReach(tool, inTheHand)
    local spec = tool.spec_horseBrush
    if spec == nil or spec.targetedHusbandry == nil then return "animal in reach: no" end
    local distance = brushTargetDistance(carryingPlayer(tool))
    if distance == nil then return "animal in reach: yes" end
    local axis = VRCompanion.HAND_TOOL_POINTING_AXES.horsebrush
    return string.format("animal in reach: yes (%.2f m)", math.max(0, distance - (inTheHand and (tonumber(axis.reachBehind) or 0) or 0)))
end

local function diagnosticFields(self)
    local tool, kind = self:getHeldHandTool()
    local hand, why = toolHandFor(self, tool)
    local toolText = "held Hand Tool: none"
    if tool ~= nil then
        toolText = kind ~= nil and ("held Hand Tool: " .. kind .. " (supported)") or "held Hand Tool: unsupported"
    end
    local function wrapStatus(name)
        if state.problem ~= nil then return name .. " wrap: gave up (" .. state.problem .. ")" end
        return name .. " wrap: " .. (wraps[name] ~= nil and "installed" or "not installed")
    end
    local wrapText = wrapStatus("attachToolToCamera")
    local lookRayWrapText = wrapStatus("getLookRay")
    local beamWrapText = wrapStatus("flashlightBeam")
    local fields = {
        string.format("Hand Tools: %s", self.handToolsEnabled ~= false and "on" or "off"),
        string.format("Tool Hand: %s", HAND_NAMES[self:getToolHand()]),
        toolText,
        hand ~= nil and "in the Tool Hand: yes" or ("in the Tool Hand: no (" .. tostring(why) .. ")"),
        describeLookRay(tool),
        wrapText,
        lookRayWrapText,
        beamWrapText,
        wrapStatus("sprayCanOnDraw"),
        wrapStatus("chainsawOnDraw"),
        wrapStatus("horseBrushOnDraw"),
        wrapStatus("horseBrushLookup"),
        "centre overlays held back: " .. ((kind ~= nil and DRAW_HELD_BACK_KINDS[kind] and drawIsHeldBack(tool)) and "yes" or "no"),
    }
    -- Haptics
    table.insert(fields, "haptics: " .. tostring(state.haptics.level or "none"))
    -- Chainsaw
    if kind == "chainsaw" then
        table.insert(fields, string.format("chainsaw: %s, cut plane heading from the %s, cut roll from the %s", tostring(self:getChainsawCutState(tool)),
            (hand ~= nil and state.chainsaw.yaw ~= nil) and "bar" or "camera",
            hand ~= nil and "wrist" or "game's roll action"))
    end
    -- Horse brush
    if kind == "horsebrush" then
        table.insert(fields, describeBrushReach(tool, hand ~= nil))
        table.insert(fields, "cleaning: " .. ((tool.spec_horseBrush or {}).targetedHusbandryId ~= nil and "yes" or "no"))
    end
    table.insert(fields, wrapStatus("chainsawOnUpdate"))
    table.insert(fields, wrapStatus("chainsawOnRoll"))
    return fields
end

-- ---- Describing an unsupported held Hand Tool (ADR-0030) ----------------------------------------------------------------------
-- While the local player holds a Hand Tool this module does not support (another mod's, a DLC's), vrHandToolsDiagnostics also
-- writes what the game gives that tool to the log, so that a log from the headset is enough to design the tool's support. It
-- only reads: no wrap, nothing about the tool, the supported list or the placement changes. Every read is guarded: a failure
-- becomes a line, never an error. Each line starts with PROBE_TAG after the mod's own tag.

local PROBE_TAG = "Hand Tool probe: "

-- The unsupported Hand Tool the local player holds, or nil (nothing held, a supported tool, another player's tool). The tool
-- the game last attached to the camera, or else the one the player says it holds (a tool the game put in the hand of the third
-- person view never went through attachToolToCamera).
local function unsupportedHeldTool()
    local tool = heldTool()
    if tool == nil then
        local player = localPlayer()
        if type(player) == "table" and type(player.getHeldHandTool) == "function" then
            local held = player:getHeldHandTool()
            tool = type(held) == "table" and held or nil
        end
    end
    if tool == nil or supportedKind(tool) ~= nil or isAnotherPlayersTool(tool) then return nil end
    return tool
end

-- The caps. Each cut says how much was left out.
local PROBE_STRING_LENGTH = 30       -- a longer string is cut
local PROBE_MAX_LINES = 400          -- lines per run, the line that says how many were left out included
local PROBE_MAX_SPEC_FIELDS = 100    -- fields shown per spec table
local PROBE_MAX_NESTED_KEYS = 40     -- a table field with more keys than this is only counted, not opened
local PROBE_MAX_FUNCTION_NAMES = 100 -- function names shown per class or type
local PROBE_MAX_NODES = 40           -- nodes shown in the tree
local PROBE_NODE_DEPTH = 3           -- levels of nodes below the root that are named

-- The keys of a table in a fixed order (a log from one run reads like the next).
local function sortedKeys(t)
    local keys = {}
    for key in pairs(t) do keys[#keys + 1] = key end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end

-- A short text for a field's value: numbers and booleans as they are, a string cut short, a table by its key count, a number
-- that is a node (an entity id) with the node's name. Nothing for a function. A node is only guessed from the number: a whole
-- number above 1 (1 is the engine's root node, and far more often a count or a direction) the engine says is an entity, whose name
-- is not a file path (images and sounds have ids too). A plain number that happens to be a node's id is still shown with its name;
-- in a log read by a person that is harmless.
local function shortValue(value)
    local kind = type(value)
    if kind == "number" then
        local text = tostring(value)
        local entityExists, getName = engine("entityExists"), engine("getName")
        if value > 1 and value == math.floor(value) and type(entityExists) == "function" and type(getName) == "function"
            and entityExists(value) then
            local name = tostring(getName(value))
            if name ~= "" and not string.find(name, "[/\\]") then text = string.format('%s (node "%s")', text, name) end
        end
        return text
    elseif kind == "boolean" then
        return tostring(value)
    elseif kind == "string" then
        if #value > PROBE_STRING_LENGTH then return '"' .. string.sub(value, 1, PROBE_STRING_LENGTH) .. '..."' end
        return '"' .. value .. '"'
    elseif kind == "table" then
        local count = 0
        for _ in pairs(value) do count = count + 1 end
        return count .. " keys"
    end
    return nil
end

-- The entries of a table field, one level further than the spec table's own fields (a state or a catch is often kept in a small
-- table): a line per entry, like a field's. A table with more keys than PROBE_MAX_NESTED_KEYS is only counted (by its own line).
local function describeNestedTable(path, value, add)
    if type(value) ~= "table" then return end
    local ok, err = pcall(function()
        local keys = sortedKeys(value)
        if #keys == 0 or #keys > PROBE_MAX_NESTED_KEYS then return end
        for _, key in ipairs(keys) do
            local entryOk, text = pcall(function()
                local entry = value[key]
                local shown = shortValue(entry)
                return string.format("%s.%s (%s)%s", path, tostring(key), type(entry), shown ~= nil and (" = " .. shown) or "")
            end)
            add(entryOk and text or string.format("%s.%s could not be read (%s)", path, tostring(key), tostring(text)))
        end
    end)
    if not ok then add(path .. " could not be opened (" .. tostring(err) .. ")") end
end

-- Every spec_ table of the tool, one level deep: a line per field with its name, Lua type and short value.
local function describeSpecTables(tool, add)
    local specNames = {}
    for _, key in ipairs(sortedKeys(tool)) do
        if type(key) == "string" and string.sub(key, 1, 5) == "spec_" and type(tool[key]) == "table" then specNames[#specNames + 1] = key end
    end
    for _, specName in ipairs(specNames) do
        local ok, err = pcall(function()
            local spec = tool[specName]
            local keys = sortedKeys(spec)
            for index, key in ipairs(keys) do
                if index > PROBE_MAX_SPEC_FIELDS then
                    add(string.format("%s: %d more fields left out", specName, #keys - PROBE_MAX_SPEC_FIELDS))
                    break
                end
                local fieldOk, text = pcall(function()
                    local value = spec[key]
                    local shown = shortValue(value)
                    return string.format("%s.%s (%s)%s", specName, tostring(key), type(value), shown ~= nil and (" = " .. shown) or "")
                end)
                add(fieldOk and text or string.format("%s.%s could not be read (%s)", specName, tostring(key), tostring(text)))
                describeNestedTable(specName .. "." .. tostring(key), spec[key], add)
            end
        end)
        if not ok then add(specName .. " could not be read (" .. tostring(err) .. ")") end
    end
end

-- The names of the functions in a table, sorted, as one text ("none" for no function); the names past the cap are counted.
local function functionNames(t)
    local names = {}
    for key, value in pairs(t) do
        if type(value) == "function" then names[#names + 1] = tostring(key) end
    end
    table.sort(names)
    if #names == 0 then return "none" end
    if #names <= PROBE_MAX_FUNCTION_NAMES then return table.concat(names, ", ") end
    local shown = {}
    for index = 1, PROBE_MAX_FUNCTION_NAMES do shown[index] = names[index] end
    return string.format("%s (and %d more left out)", table.concat(shown, ", "), #names - PROBE_MAX_FUNCTION_NAMES)
end

-- The name a global table is kept under, or nil.
local function globalNameOf(value)
    for name, candidate in pairs(_G) do
        if candidate == value and type(name) == "string" then return name end
    end
    return nil
end

-- Each specialization of the tool: its class (the game keeps it next to the specialization's name) with the global name it has,
-- and the functions on it, where handlers kept on the class only (the chainsaw's, the horse brush's) are.
local function describeSpecializationClasses(tool, add)
    local names, classes = tool.specializationNames, tool.specializations
    if type(names) ~= "table" or type(classes) ~= "table" then return end
    for index, specName in ipairs(names) do
        local class = classes[index]
        if type(class) == "table" then
            local ok, err = pcall(function()
                add(string.format("specialization %s: class %s, functions: %s", tostring(specName),
                    globalNameOf(class) or "(no global name)", functionNames(class)))
            end)
            if not ok then add(string.format("specialization %s: class could not be read (%s)", tostring(specName), tostring(err))) end
        end
    end
end

-- The functions the tool's type gives it.
local function describeTypeFunctions(tool, add)
    local toolType = tool.type
    if type(toolType) ~= "table" or type(toolType.functions) ~= "table" then return end
    add(string.format("type %s: functions: %s", tostring(toolType.name or tool.typeName), functionNames(toolType.functions)))
end

-- What the callback of an action event is called: found by looking it up among the functions of the specialization classes (named
-- by the global the class is kept under), of the tool's type and of the tool itself.
local function callbackName(tool, callback)
    if type(callback) ~= "function" then return nil end
    if type(tool.specializations) == "table" then
        for _, class in ipairs(tool.specializations) do
            if type(class) == "table" then
                for name, fn in pairs(class) do
                    if fn == callback then return (globalNameOf(class) or "(class)") .. "." .. tostring(name) end
                end
            end
        end
    end
    local toolType = tool.type
    if type(toolType) == "table" and type(toolType.functions) == "table" then
        for name, fn in pairs(toolType.functions) do
            if fn == callback then return "type function " .. tostring(name) end
        end
    end
    for name, fn in pairs(tool) do
        if fn == callback then return "tool." .. tostring(name) end
    end
    return nil
end

-- The field names of a table, sorted, as one text.
local function fieldNames(t)
    local names = {}
    for _, key in ipairs(sortedKeys(t)) do names[#names + 1] = tostring(key) end
    return table.concat(names, ", ")
end

-- The edges an action event fires on, as registerActionEvent takes them: triggerDown (the press), triggerUp (the release) and
-- triggerAlways (every frame the input is held).
local function edgesOf(read)
    local down, up, always = read("triggerDown"), read("triggerUp"), read("triggerAlways")
    if type(down) ~= "boolean" and type(up) ~= "boolean" and type(always) ~= "boolean" then return nil end
    local edges = {}
    if down == true then edges[#edges + 1] = "press" end
    if up == true then edges[#edges + 1] = "release" end
    if always == true then edges[#edges + 1] = "every frame" end
    return #edges > 0 and table.concat(edges, ", ") or "nothing"
end

-- The tool's registered action events, as the game keeps them: the action's name, the callback's name where it can be found, and
-- the edges. The game may keep the details with the input binding's event (named by `actionEventId`): read from there too.
local function describeActionEvents(tool, add)
    local events = tool.actionEvents
    if type(events) ~= "table" then return end
    local inputBinding = engine("g_inputBinding")
    local lines = {}
    for key, entry in pairs(events) do
        local ok, err = pcall(function()
            if type(entry) ~= "table" then return end
            local bound = type(inputBinding) == "table" and type(inputBinding.events) == "table"
                and entry.actionEventId ~= nil and inputBinding.events[entry.actionEventId] or nil
            local function read(field)
                local value = entry[field]
                if value == nil and type(bound) == "table" then value = bound[field] end
                return value
            end
            local actionName = read("actionName") or (type(key) == "string" and key or nil) or "(unnamed)"
            local callback = read("callback")
            local name = callbackName(tool, callback)
            local callbackText = name ~= nil and ("callback " .. name)
                or (type(callback) == "function" and "callback not found among the class and type functions" or "callback not readable")
            local edges = edgesOf(read)
            local edgesText = edges ~= nil and ("fires on: " .. edges) or ("edges not readable (fields: " .. fieldNames(entry) .. ")")
            lines[#lines + 1] = string.format("action event %s: %s, %s", tostring(actionName), callbackText, edgesText)
        end)
        if not ok then lines[#lines + 1] = string.format("action event %s could not be read (%s)", tostring(key), tostring(err)) end
    end
    table.sort(lines)
    for _, line in ipairs(lines) do add(line) end
end

-- The node tree below the tool's root, by name (the path from the root), to a fixed depth and a fixed number of nodes. A node at
-- the last level says how many children it has that are not listed; the nodes past the cap are counted.
local function describeNodes(tool, add)
    local root = tool.rootNode
    local getName, getNumOfChildren, getChildAt = engine("getName"), engine("getNumOfChildren"), engine("getChildAt")
    if root == nil or type(getName) ~= "function" or type(getNumOfChildren) ~= "function" or type(getChildAt) ~= "function" then
        add("node tree: the tool has no root node to read")
        return
    end
    local shown, leftOut = 0, 0
    local function walk(node, path, depth)  -- depth: the levels of `node` below the root
        local count = getNumOfChildren(node)
        for index = 0, count - 1 do
            local child = getChildAt(node, index)
            local childPath = path .. "/" .. tostring(getName(child))
            local notListed = (depth + 1 == PROBE_NODE_DEPTH) and getNumOfChildren(child) or 0
            if shown < PROBE_MAX_NODES then
                shown = shown + 1
                add("node " .. childPath .. (notListed > 0 and string.format(" (%d child%s not listed)", notListed, notListed == 1 and "" or "ren") or ""))
            else
                leftOut = leftOut + 1
            end
            if depth + 1 < PROBE_NODE_DEPTH then walk(child, childPath, depth + 1) end
        end
    end
    -- A node that cannot be read ends the walk, not the description: the lines already added stay, and so does the count of the
    -- nodes left out before the failure (the failure is a line after it).
    local ok, err = pcall(function() walk(root, tostring(getName(root)), 0) end)
    if leftOut > 0 then add(string.format("node tree: %d more nodes left out", leftOut)) end
    if not ok then add("node tree could not be read to the end (" .. tostring(err) .. ")") end
end

-- Whether the game's last placement of the tool was HandTool:attachToolToCamera (it links the tool to the camera when the player
-- is in the first person view) or another path (attachToolToHand in the third person view, or whatever else a tool does). This is
-- only known from the wrap of attachToolToCamera, which noted the tool when the game placed it (state.cameraPlacedTool); without
-- the wrap (the game's function is missing or reshaped, or Hand Tools gave up) nothing was noted, and the path is said to be
-- unknown, not another one. The game places a held tool again whenever the view changes, so a tool noted in the first person
-- view that is now seen from the third person view has been placed by another path since (nothing is wrapped to see that).
local function describePlacement(tool, add)
    if wraps.attachToolToCamera == nil then
        add("placement: unknown (the placement wrap is not installed)")
        return
    end
    local player = carryingPlayer(tool) or localPlayer()
    local camera = type(player) == "table" and player.camera or nil
    local inFirstPerson = type(camera) == "table" and camera.isFirstPerson == true
    if state.cameraPlacedTool == tool and inFirstPerson then
        add("placement: through attachToolToCamera")
    else
        add("placement: not through attachToolToCamera (the game used another path)")
    end
end

-- Runs one part of the description; a failure becomes a line, and the other parts go on.
local function guarded(what, part, tool, add)
    local ok, err = pcall(part, tool, add)
    if not ok then add(what .. " could not be read (" .. tostring(err) .. ")") end
end

-- The lines that describe the tool, without the tags, at most PROBE_MAX_LINES in all: when lines are left out, the last of those
-- lines is the one that counts them (it takes the place of the last line kept, which is counted too). The parts that are short and
-- tell most come first; the spec tables, which have no limit of their own, come last and are what a cut takes.
local function describeUnsupportedTool(tool)
    local lines, leftOut = {}, 0
    local function add(line)
        if #lines < PROBE_MAX_LINES then lines[#lines + 1] = line else leftOut = leftOut + 1 end
    end
    guarded("config file", function(t) add("config file: " .. tostring(t.configFileName)) end, tool, add)
    guarded("placement", describePlacement, tool, add)
    guarded("specializations", describeSpecializationClasses, tool, add)
    guarded("type", describeTypeFunctions, tool, add)
    guarded("action events", describeActionEvents, tool, add)
    guarded("node tree", describeNodes, tool, add)
    guarded("spec tables", describeSpecTables, tool, add)
    if leftOut > 0 then lines[#lines] = string.format("%d more lines left out", leftOut + 1) end
    return lines
end

-- The description of the unsupported tool held now, [] when there is none.
local function probeLines()
    local ok, result = pcall(function()
        local tool = unsupportedHeldTool()
        if tool == nil then return {} end
        return describeUnsupportedTool(tool)
    end)
    if ok then return result end
    return { "could not be read: " .. tostring(result) }
end

-- One line (the Pointer diagnostics may carry it too).
function VRCompanion:getHandToolsDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

-- Prints the fields to the game log, one tagged line each, and returns them as one line. A held unsupported Hand Tool is
-- described after them.
function VRCompanion:consoleCommandHandToolsDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    for _, line in ipairs(probeLines()) do
        print("[FS25_VR] " .. PROBE_TAG .. line)
    end
    return self:getHandToolsDiagnostics()
end

-- ---- Tuning: vrHandToolOffset -------------------------------------------------------------------------------------------
-- Moves or turns a tool in the Tool Hand while the game runs, to find the numbers of HAND_TOOL_HANDLE_OFFSETS in the headset
-- (this session only: the table in this file is what ships). The answer, and the log, carry the resulting numbers as the
-- table has them, ready to be written down.

local MAX_NUDGE_METRES = 0.5
local MAX_TURN_DEGREES = 360

local function quaternionProduct(a, b)
    return {
        w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    }
end

local function quaternionAbout(axis, degrees)
    local half = math.rad(degrees) / 2
    local s = math.sin(half)
    return { x = axis == "x" and s or 0.0, y = axis == "y" and s or 0.0, z = axis == "z" and s or 0.0, w = math.cos(half) }
end

local function describeOffset(kind, offset)
    return string.format("%s: position = { x = %.4f, y = %.4f, z = %.4f }, rotation = { x = %.4f, y = %.4f, z = %.4f, w = %.4f }",
        kind, offset.position.x, offset.position.y, offset.position.z,
        offset.rotation.x, offset.rotation.y, offset.rotation.z, offset.rotation.w)
end

local TUNING_USAGE = "Usage: vrHandToolOffset <lance|flashlight|chainsaw|spraycan|horsebrush|fishingrod> <nudge dx dy dz | turn rx ry rz | reset> "
    .. "(nudge: metres, turn: degrees about the hand's X, Y, Z axes; this session only)"

-- The held tool is put where the numbers say, at once.
local function replaceHeldToolInTheHand(self)
    local tool = state.linkedTool
    if tool ~= nil and state.linkedHand ~= nil and heldTool() == tool then
        linkIntoHand(self, tool, state.linkedHand)
    end
end

function VRCompanion:consoleCommandHandToolOffset(kind, action, a, b, c)
    local offsets = self.HAND_TOOL_HANDLE_OFFSETS
    local name = kind ~= nil and string.lower(tostring(kind)) or ""
    if name == "" or name == "status" then
        local lines = {}
        for _, entry in ipairs(SUPPORTED_TOOLS) do
            table.insert(lines, describeOffset(entry.kind, offsets[entry.kind]))
        end
        return table.concat(lines, "\n") .. "\n" .. TUNING_USAGE
    end
    local offset = offsets[name]
    if offset == nil then return TUNING_USAGE end
    action = action ~= nil and string.lower(tostring(action)) or "status"

    if action == "status" then
        return describeOffset(name, offset)
    elseif action == "reset" then
        offsets[name] = copyOffset(DEFAULT_HANDLE_OFFSETS[name])
    elseif action == "nudge" then
        local dx, dy, dz = tonumber(a), tonumber(b), tonumber(c)
        if dx == nil or dy == nil or dz == nil or math.abs(dx) > MAX_NUDGE_METRES or math.abs(dy) > MAX_NUDGE_METRES
            or math.abs(dz) > MAX_NUDGE_METRES then
            return TUNING_USAGE
        end
        offset.position.x, offset.position.y, offset.position.z = offset.position.x + dx, offset.position.y + dy, offset.position.z + dz
    elseif action == "turn" then
        local rx, ry, rz = tonumber(a), tonumber(b), tonumber(c)
        if rx == nil or ry == nil or rz == nil or math.abs(rx) > MAX_TURN_DEGREES or math.abs(ry) > MAX_TURN_DEGREES
            or math.abs(rz) > MAX_TURN_DEGREES then
            return TUNING_USAGE
        end
        -- About the hand's own axes: the turn goes in front of the tool's rotation in the grip frame.
        local turn = quaternionProduct(quaternionAbout("z", rz), quaternionProduct(quaternionAbout("y", ry), quaternionAbout("x", rx)))
        local q = quaternionProduct(turn, { x = offset.rotation.x, y = offset.rotation.y, z = offset.rotation.z, w = offset.rotation.w })
        local length = math.sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)
        offset.rotation = { x = q.x / length, y = q.y / length, z = q.z / length, w = q.w / length }
    else
        return TUNING_USAGE
    end

    offset = offsets[name]
    replaceHeldToolInTheHand(self)
    local text = describeOffset(name, offset)
    print("[FS25_VR] Hand Tools offset " .. text)
    return text
end

function VRCompanion:registerHandToolsConsoleCommands()
    wrapAll()    -- map load: the class again (resetHandTools lifted the wrap with the last map)
    checkWraps()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrHandToolOffset", "Move or turn a Hand Tool in the Tool Hand while you look at it, this session only: vrHandToolOffset <lance|flashlight|chainsaw|spraycan|horsebrush|fishingrod> <nudge dx dy dz | turn rx ry rz | reset>", "consoleCommandHandToolOffset", self)
    addConsoleCommand("vrHandTools", "Turn Hand Tools, the flashlight, chainsaw, pressure washer lance, spray can, horse brush and fishing rod held in a Motion Controller, on or off (saved): vrHandTools [on|off]", "consoleCommandSetHandTools", self)
    addConsoleCommand("vrToolHand", "Choose the Motion Controller that holds Hand Tools, left or right (saved; right by default): vrToolHand [left|right]", "consoleCommandSetToolHand", self)
    addConsoleCommand("vrHandToolsDiagnostics", "Report the Tool Hand, the held Hand Tool, whether it is in the hand and where the look ray starts: vrHandToolsDiagnostics", "consoleCommandHandToolsDiagnostics", self)
end

-- Called by the Virtual Hands just before they delete their nodes: the tool hangs below one, and deleting the node would
-- delete the tool. It goes back to where the game puts it.
function VRCompanion:releaseHandToolsFromHands()
    local tool = state.linkedTool
    endTriggerUse()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    forgetLink()
end

-- Map unload: the tool goes back to the game's placement (the Virtual Hand it hangs below is deleted next), the wraps come
-- off and everything seen this session is forgotten. The saved setting stays as it is.
function VRCompanion:resetHandTools()
    local tool = state.linkedTool
    endTriggerUse()
    liftWraps()
    if tool ~= nil and heldTool() == tool then pcall(returnToTheGame, tool) end
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrHandTools")
        removeConsoleCommand("vrToolHand")
        removeConsoleCommand("vrHandToolOffset")
        removeConsoleCommand("vrHandToolsDiagnostics")
    end
    state = newState()
end

wrapAll()  -- the game's HandTool is there as the files load

VRCompanion.HandTools.updateHandTools = function(self, ...) return VRCompanion.updateHandTools(VRCompanion, ...) end
VRCompanion.HandTools.isHandHoldingHandTool = function(self, ...) return VRCompanion.isHandHoldingHandTool(VRCompanion, ...) end
VRCompanion.HandTools.getToolHand = function(self, ...) return VRCompanion.getToolHand(VRCompanion, ...) end
VRCompanion.HandTools.consoleCommandSetToolHand = function(self, ...) return VRCompanion.consoleCommandSetToolHand(VRCompanion, ...) end
VRCompanion.HandTools.consoleCommandSetHandTools = function(self, ...) return VRCompanion.consoleCommandSetHandTools(VRCompanion, ...) end
VRCompanion.HandTools.consoleCommandHandToolsDiagnostics = function(self, ...) return VRCompanion.consoleCommandHandToolsDiagnostics(VRCompanion, ...) end

return VRCompanion.HandTools
