#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace fs25vr {

// The Companion Mod's settings.xml, read by the Injector for the settings it needs before a savegame has loaded: the
// Companion Mod only runs once one has, but the main menu is shown before that. The Companion Mod writes the file; the
// Injector only ever reads it.

// <Documents>\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml of the user running the game, where
// Documents is the known folder (so a OneDrive-redirected one too), as the Installer assumes. Empty when the Documents
// folder cannot be found.
std::filesystem::path DefaultCompanionSettingsPath();

// The number in the first <name>...</name> element of a Companion Mod settings file (the engine writes a setting
// "vrSettings.name" as such an element under the <vrSettings> root). nullopt when the file cannot be read, has no such
// element, or its text is not a finite number.
std::optional<float> ReadCompanionSettingFloat(const std::filesystem::path& file, std::string_view name);

} // namespace fs25vr
