#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace fs25vr {

std::filesystem::path DefaultCompanionSettingsPath();

std::optional<float> ReadCompanionSettingFloat(const std::filesystem::path& file, std::string_view name);

}
