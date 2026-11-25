VRCompanion = VRCompanion or {}

function VRCompanion.getLocalisedText(key, englishText)
    local i18n = VRCompanion.getEngineGlobal("g_i18n")
    if type(key) ~= "string" or key == "" or type(i18n) ~= "table" then return englishText end
    local ok, text = pcall(function()
        if i18n:hasText(key) then return i18n:getText(key) end
        return nil
    end)
    if ok and type(text) == "string" and text ~= "" then return text end
    return englishText
end

function VRCompanion.formatLocalisedText(key, englishFormat, ...)
    local ok, text = pcall(string.format, VRCompanion.getLocalisedText(key, englishFormat), ...)
    if ok then return text end
    return string.format(englishFormat, ...)
end

function VRCompanion.onOffText(enabled)
    if enabled then return VRCompanion.getLocalisedText("vrNotification_on", "on") end
    return VRCompanion.getLocalisedText("vrNotification_off", "off")
end
