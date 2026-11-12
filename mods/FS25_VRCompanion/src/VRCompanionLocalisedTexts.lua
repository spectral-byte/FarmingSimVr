-- ====================================================================
-- FS25 VR Companion Mod: localised texts
--
-- The texts the player reads in the game's language: an l10n key's text (the game's own, or this mod's from modDesc.xml's <l10n>),
-- with today's English text where the game has none. Notifications and blinking warnings are made with these; console output and
-- log lines stay English. Loads right after the core, whose getEngineGlobal it reads g_i18n through.
-- ====================================================================

VRCompanion = VRCompanion or {}

-- The text the player reads for an l10n key (the game's own, or this mod's from modDesc.xml) in the game's current language,
-- else englishText: when the game has no text for the key, has no g_i18n, or asking it raises. A key is never returned as the
-- text. englishText may be nil, for a caller that tries something else next.
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

-- A notification's text: the text for an l10n key (see getLocalisedText) formatted with the values, else englishFormat formatted
-- with them, also when the game's text does not take these values. Console output and log lines are not made with it: they stay
-- English.
function VRCompanion.formatLocalisedText(key, englishFormat, ...)
    local ok, text = pcall(string.format, VRCompanion.getLocalisedText(key, englishFormat), ...)
    if ok then return text end
    return string.format(englishFormat, ...)
end

-- "on" or "off" in the game's language, for a notification that a setting was turned on or off.
function VRCompanion.onOffText(enabled)
    if enabled then return VRCompanion.getLocalisedText("vrNotification_on", "on") end
    return VRCompanion.getLocalisedText("vrNotification_off", "off")
end
