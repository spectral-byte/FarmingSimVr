#pragma once

namespace fs25vr {

// The Injector's crash reporter: the process's top-level unhandled exception filter, so it sees crashes only, never
// the exceptions the game handles. When the process crashes it finishes fs25_vr.log (flushing the lines still
// buffered), adds one line that names the crash, writes a small minidump next to the log, then passes the crash on to
// the filter that was installed before it, so the game's own crash handling and Windows Error Reporting run as before.
// With no earlier filter it lets Windows continue its own handling.
//
// It runs once per process. A second crash, or a crash inside the reporter, goes straight to the earlier filter.
// Inside a crashing process it does nothing risky: it loads no library, creates no thread and never waits for a lock,
// the log's lock included (the crashed thread may hold it, and then the line is skipped). The crashing thread itself
// does next to nothing, because after a stack overflow it has next to no stack: it hands the exception to a helper
// thread that was prepared ahead of time (PrepareCrashDump) and waits for it with a time limit, so that a stuck helper
// cannot hang the game's exit. The helper writes the crash line and then the dump, with the system's dbghelp.dll.
// Before the first DXGI factory call there is no helper: the crashing thread writes the line itself when its stack has
// room for it, and leaves it out when it has not.

// Puts the reporter on top of the unhandled exception filter chain. Loader-lock safe: loads no library and installs
// no hook, so DllMain may call it (StartInjector does). Calling it again is harmless: when another filter was
// installed on top meanwhile, that one becomes the filter to pass crashes on to.
void InstallCrashReporter();

// Puts the reporter on top again after another module took the place (the game may install a filter of its own after
// the Injector started), but only when InstallCrashReporter ran: the tests that drive the DXGI proxy in-process must
// not leave a filter behind. A different filter found on top becomes the first one to pass crashes on to; the ones from
// before follow it. When none is on top, the earlier ones are forgotten too: whoever removed the reporter removed the
// chain with it, and a filter taken away may sit in a module that is gone, so the crash goes to Windows' own handling
// instead.
//
// A filter on top that called the one it replaced (the reporter) from its own makes a loop with the reporter, which
// passes the crash on to it: the reporter, reached again from inside a filter, passes the crash on to the next older
// one, so that each is called once. The Injector calls this at the first DXGI factory call (PrepareCrashDump) and when
// it attaches to the game's swap chain, which it hooks in Active VR Mode only.
void ReinstallCrashReporter();

// Prepares the crash dump, once per process, at the first DXGI factory call, where the Injector's other deferred
// start-up runs: puts the reporter on top again, loads the system's dbghelp.dll by its full path and starts the helper
// thread that waits for a crash. Never from DllMain (it loads a library) and never from the filter itself. Does
// nothing when InstallCrashReporter did not run. A crash before this point gets the crash line only, which says why.
void PrepareCrashDump();

// Takes the reporter off again, but only when it is still the top filter: a filter installed after it may have kept
// a pointer to it. Does nothing when it was never installed. Also ends the dump helper thread, since its code goes
// away with the Injector. For the Injector's unload with FreeLibrary; at process termination the reporter is left in
// place.
void RemoveCrashReporter();

} // namespace fs25vr
