#pragma once

namespace fs25vr {

inline constexpr unsigned long kCrashDumpFlags = 0x1060;

void InstallCrashReporter();

void ReinstallCrashReporter();

void PrepareCrashDump();

void RemoveCrashReporter();

}
