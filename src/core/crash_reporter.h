#pragma once

namespace fs25vr {

// The Injector's crash reporter: the process's top-level unhandled exception filter, so it sees crashes only, never
// the exceptions the game handles. When the process crashes it finishes fs25_vr.log (flushing the lines still
// buffered) and adds one line that names the crash, then passes the crash on to the filter that was installed before
// it, so the game's own crash handling and Windows Error Reporting run as before. With no earlier filter it lets
// Windows continue its own handling.
//
// It runs once per process. A second crash, or a crash inside the reporter, goes straight to the earlier filter.
// Inside a crashing process it does nothing risky: it loads no library and never waits for a lock, the log's lock
// included (the crashed thread may hold it, and then the line is skipped).

// Puts the reporter on top of the unhandled exception filter chain. Loader-lock safe: loads no library and installs
// no hook, so DllMain may call it (StartInjector does). Calling it again is harmless: when another filter was
// installed on top meanwhile, that one becomes the filter to pass crashes on to.
void InstallCrashReporter();

// Takes the reporter off again, but only when it is still the top filter: a filter installed after it may have kept
// a pointer to it. Does nothing when it was never installed. For the Injector's unload with FreeLibrary; at process
// termination the reporter is left in place.
void RemoveCrashReporter();

} // namespace fs25vr
