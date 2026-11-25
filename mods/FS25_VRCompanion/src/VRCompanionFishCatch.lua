VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_FISH_CATCH_ANSWER = "ask"
VRCompanion.fishCatchAnswer = VRCompanion.fishCatchAnswer or VRCompanion.DEFAULT_FISH_CATCH_ANSWER

local SETTING_KEY_ANSWER = "vrSettings.fishCatchAnswer"
local ANSWERS = { ask = true, sell = true, release = true }
local NOTIFICATION_MS = 2500

local RETRY_TICKS = 20
local MAX_ATTEMPTS = 5

local answeredDialog = nil
local attempts = 0
local lastAttemptTick = nil
local pendingNotice = nil
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
        if pendingNotice ~= nil then showNotification(pendingNotice) end
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
            pendingNotice = nil
            print(string.format("[FS25_VR] vrFishCatch %s: the dialog is still open after %d attempts; it is left for the player.", choice, attempts))
        end
        return
    end

    local dialogTarget = dialog.target
    local methodName = choice == "sell" and "onYes" or "onNo"
    if type(dialogTarget) ~= "table" or type(dialogTarget[methodName]) ~= "function" then
        attempts = MAX_ATTEMPTS
        if not warnedNoMethod then
            warnedNoMethod = true
            print(string.format("[FS25_VR] vrFishCatch %s: the fish dialog has no %s to call; it is left open for the player.", choice, methodName))
        end
        return
    end

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
        showNotification(pendingNotice)
        pendingNotice = nil
    end
end

function VRCompanion:setFishCatchAnswer(answer, save)
    if ANSWERS[answer] ~= true then return end
    self.fishCatchAnswer = answer
    if save then
        self:saveConfiguration()
    end
end

function VRCompanion:saveFishCatchSetting(xmlFile, xml)
    if type(xml.setString) == "function" then
        xml.setString(xmlFile, SETTING_KEY_ANSWER, self.fishCatchAnswer or self.DEFAULT_FISH_CATCH_ANSWER)
    end
end

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

VRCompanion:registerSubsystem("fishCatch", {
    updateEarly = function(self) self:updateFishCatch() end,
    saveSettings = function(self, xmlFile, xml) self:saveFishCatchSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadFishCatchSetting(xmlFile, xml) end,
    mapUnload = function(self)
        forgetDialog()
        warnedNoMethod = false
    end,
    consoleCommands = {
        { name = "vrFishCatch", description = "Answer the fish catch dialog (Highlands Fishing DLC) as it opens, with a notification of the catch (saved; ask by default): vrFishCatch [ask|sell|release]", method = "consoleCommandSetFishCatch" },
    },
})
