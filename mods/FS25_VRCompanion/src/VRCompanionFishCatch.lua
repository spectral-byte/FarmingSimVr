-- ====================================================================
-- FS25 VR Companion Mod: Fish catch auto-answer
--
-- The saved console command `vrFishCatch [ask|sell|release]` (spec #142, ADR-0032) answers the fish catch dialog (Highlands Fishing
-- DLC), the Stereo Dialog, as it opens, so the player never sees it, and shows a short notification of the catch instead. It
-- applies in the stereoscopic view and in 2D Floating Window Mode alike. A flat choice: no rule by weight, species or price.
--
-- PUBLIC SURFACE
--   VRCompanion.fishCatchAnswer                  "ask" | "sell" | "release", default "ask", saved (`vrFishCatch`); `ask` leaves the dialog alone
--   VRCompanion:updateFishCatch()                once per update, before the GUI is polled (VRCompanionCore.lua runUpdate)
--   VRCompanion:registerFishCatchConsoleCommands() / resetFishCatch()   map load / unload
--   Console: vrFishCatch [ask|sell|release]      no argument reports the value, an invalid one is refused with the usage
--
-- THE ANSWER PATH (chosen for ticket #147): the dialog's own answer method, `target:onYes()` for SELL and `target:onNo()` for RELEASE,
-- where `target` is the dialog root's controller (the Stereo Dialog found by VRCompanion:findStereoDialog). The headset probe (ticket
-- #143) found that the dialog is a generic yes/no dialog whose class chain has both methods and whose yesButton is SELL and noButton is
-- RELEASE; the engine's own YesNoDialog:inputEvent calls self:onNo() for the back action, so calling it is the same step the dialog
-- itself takes. That a direct call closes the dialog and sends the callback is inferred, not yet verified in the headset.
-- A dialog without the method is left open (the player answers it with the buttons, as with `ask`) and the game log says so once: the
-- Virtual Gamepad only mirrors the Motion Controllers (ADR-0028), so the mod cannot press a pad button of its own.
-- Called before the GUI poll, the answer closes the dialog before the poll sees it, so the quad of 2D Floating Window Mode does not
-- flash for it either. Once per opening: the dialog that was answered is remembered until it is gone.
--
-- THE NOTIFICATION: the dialog holds no species, weight or price field, only its text (target.dialogTextElement.text, measured:
-- "You've caught a trout!\n\nDo you want to release the fish to the water or sell it for 100 $?"). Where the text matches that
-- template, the notification names the species (and the price when sold); otherwise it is "Fish sold" / "Fish released". There is no weight.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.FishCatch = VRCompanion.FishCatch or {}

VRCompanion.DEFAULT_FISH_CATCH_ANSWER = "ask"
VRCompanion.fishCatchAnswer = VRCompanion.fishCatchAnswer or VRCompanion.DEFAULT_FISH_CATCH_ANSWER

local SETTING_KEY_ANSWER = "vrSettings.fishCatchAnswer"
local ANSWERS = { ask = true, sell = true, release = true }
local NOTIFICATION_MS = 2500

-- An answer the dialog ignores is asked again: a dialog ignores answers for a short time after it opens (FrameElement
-- inputDisableTime), and the first headset test found the dialog still up after a single call on the first tick. Every RETRY_TICKS
-- updates, at most MAX_ATTEMPTS times, and not at all while the dialog says its input is disabled.
local RETRY_TICKS = 20
local MAX_ATTEMPTS = 5

-- The dialog being answered, until it is gone, and how the answer is going
local answeredDialog = nil
local attempts = 0
local lastAttemptTick = nil
local pendingNotice = nil       -- announced once the dialog is gone after an attempt, not before: the call may have done nothing
local loggedWaiting = false
local warnedNoMethod = false

local function forgetDialog()
    answeredDialog = nil
    attempts = 0
    lastAttemptTick = nil
    pendingNotice = nil
    loggedWaiting = false
end

local function showNotification(text)
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function() mission:showBlinkingWarning(text, NOTIFICATION_MS) end)
    end
end

-- Species and price from the dialog's text, nil where it is not the template the probe measured
local function readCatch(dialogTarget)
    local ok, text = pcall(function() return dialogTarget.dialogTextElement.text end)
    if not ok or type(text) ~= "string" then return nil, nil end
    local species, price = string.match(text, "^You've caught an? ([^!\n]+)!%s+Do you want to release the fish to the water or sell it for ([^\n?]+)%?%s*$")
    return species, price
end

local function notificationText(choice, species, price)
    if choice == "sell" then
        if species ~= nil then return string.format("Sold %s for %s", species, price) end
        return "Fish sold"
    end
    if species ~= nil then return string.format("Released %s", species) end
    return "Fish released"
end

function VRCompanion:updateFishCatch()
    local choice = self.fishCatchAnswer
    local dialog = self:findStereoDialog()
    if dialog == nil then
        if pendingNotice ~= nil then showNotification(pendingNotice) end  -- gone after an attempt: it took
        forgetDialog()
        return
    end
    if choice ~= "sell" and choice ~= "release" then return end
    if dialog ~= answeredDialog then
        forgetDialog()
        answeredDialog = dialog
    end

    local tick = self.frameTicks or 0
    if lastAttemptTick ~= nil and tick - lastAttemptTick < RETRY_TICKS then return end
    if attempts >= MAX_ATTEMPTS then
        if pendingNotice ~= nil then
            pendingNotice = nil  -- whoever closes it now, it was not the mod
            print(string.format("[FS25_VR] vrFishCatch %s: the dialog is still open after %d attempts; it is left for the player.", choice, attempts))
        end
        return
    end

    local dialogTarget = dialog.target
    local methodName = choice == "sell" and "onYes" or "onNo"
    if type(dialogTarget) ~= "table" or type(dialogTarget[methodName]) ~= "function" then
        attempts = MAX_ATTEMPTS  -- not tried again for this opening
        if not warnedNoMethod then
            warnedNoMethod = true
            print(string.format("[FS25_VR] vrFishCatch %s: the fish dialog has no %s to call; it is left open for the player.", choice, methodName))
        end
        return
    end

    -- Not while the dialog says it takes no input yet: it would ignore the call
    if type(dialogTarget.isInputDisabled) == "function" then
        local okDisabled, disabled = pcall(dialogTarget.isInputDisabled, dialogTarget)
        if okDisabled and disabled then
            if not loggedWaiting then
                loggedWaiting = true
                print(string.format("[FS25_VR] vrFishCatch %s: waiting for the fish dialog to take input.", choice))
            end
            return
        end
    end

    attempts = attempts + 1
    lastAttemptTick = tick
    if pendingNotice == nil then
        local species, price = readCatch(dialogTarget)
        pendingNotice = notificationText(choice, species, price)
    end
    print(string.format("[FS25_VR] vrFishCatch %s: calling the dialog's %s (attempt %d).", choice, methodName, attempts))
    local ok, err = pcall(dialogTarget[methodName], dialogTarget)
    if not ok then
        print(string.format("[FS25_VR] vrFishCatch %s: calling the dialog's %s failed: %s", choice, methodName, tostring(err)))
        return
    end
    if self:findStereoDialog() ~= dialog then
        showNotification(pendingNotice)  -- it closed at once
        pendingNotice = nil
    end
end

function VRCompanion:setFishCatchAnswer(answer, save)
    if ANSWERS[answer] ~= true then return end
    self.fishCatchAnswer = answer
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

function VRCompanion:saveFishCatchSetting(xmlFile, xml)
    if type(xml.setString) == "function" then
        xml.setString(xmlFile, SETTING_KEY_ANSWER, self.fishCatchAnswer or self.DEFAULT_FISH_CATCH_ANSWER)
    end
end

-- A key that is missing (or not one of the answers) keeps the current value.
function VRCompanion:loadFishCatchSetting(xmlFile, xml)
    if type(xml.getString) ~= "function" then return end
    local str = xml.getString(xmlFile, SETTING_KEY_ANSWER)
    local answer = type(str) == "string" and string.lower(str) or nil
    if answer ~= nil and ANSWERS[answer] == true then self.fishCatchAnswer = answer end
end

function VRCompanion:consoleCommandSetFishCatch(arg)
    local usage = "Usage: vrFishCatch [ask|sell|release] (saved)"
    if arg == nil or arg == "" then
        return string.format("Fish catch answer: %s. %s", self.fishCatchAnswer or self.DEFAULT_FISH_CATCH_ANSWER, usage)
    end
    local answer = string.lower(tostring(arg))
    if ANSWERS[answer] ~= true then
        return usage
    end
    self:setFishCatchAnswer(answer, true)
    print(string.format("[FS25_VR] Fish catch answer set to: %s", answer))
    return string.format("Fish catch answer set to: %s (Saved)", answer)
end

function VRCompanion:registerFishCatchConsoleCommands()
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrFishCatch", "Answer the fish catch dialog (Highlands Fishing DLC) as it opens, with a notification of the catch (saved; ask by default): vrFishCatch [ask|sell|release]", "consoleCommandSetFishCatch", self)
end

-- Map unload: the dialog that was answered is forgotten. The saved setting stays as it is.
function VRCompanion:resetFishCatch()
    forgetDialog()
    warnedNoMethod = false
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrFishCatch")
    end
end

VRCompanion.FishCatch.updateFishCatch = function(self, ...) return VRCompanion.updateFishCatch(VRCompanion, ...) end
VRCompanion.FishCatch.consoleCommandSetFishCatch = function(self, ...) return VRCompanion.consoleCommandSetFishCatch(VRCompanion, ...) end

return VRCompanion.FishCatch
