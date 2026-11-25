#pragma once

#include <string_view>

#ifndef FS25VR_VERSION
#error FS25VR_VERSION must be defined by the build (CMakeLists.txt reads it from the Companion Mod's modDesc.xml)
#endif

namespace fs25vr {

inline constexpr std::string_view kInjectorVersion = FS25VR_VERSION;

}
