#pragma once

#include <string_view>

#ifndef FS25VR_VERSION
#error FS25VR_VERSION must be defined by the build (CMakeLists.txt reads it from the Companion Mod's modDesc.xml)
#endif

namespace fs25vr {

// The Injector's release version as built, for example "1.2.0.0": the <version> of the Companion Mod's modDesc.xml,
// the same number the build writes into the Injector's version resource. The Injector and the Companion Mod are
// released together, so there is one number to bump. A constant rather than something found at run time, because
// the first log line names it from DllMain, before any subsystem exists.
inline constexpr std::string_view kInjectorVersion = FS25VR_VERSION;

} // namespace fs25vr
