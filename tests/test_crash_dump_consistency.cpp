// The crash dump numbers and commands are written down in several places that nothing else ties together: the Injector,
// the Installer script, the README, the README.txt that scripts/package_release.ps1 writes into the bundle, ADR-0039
// and the Installer test. These tests read those files as text, so that changing one and forgetting another fails here.
#include <gtest/gtest.h>
#include "companion_mod_fixture.h"
#include "core/crash_reporter.h"
#include <cstdint>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> LinesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// The command lines a player is told to type: a PowerShell one-liner or a reg command, on a line of its own.
std::vector<std::string> CommandLinesIn(const std::string& text) {
    std::vector<std::string> commands;
    for (const std::string& line : LinesOf(text)) {
        if (line.rfind("powershell -NoProfile -Command ", 0) == 0 || line.rfind("reg ", 0) == 0) commands.push_back(line);
    }
    return commands;
}

// The README.txt that package_release.ps1 writes: the text of its here-string, in which a dollar sign is escaped with
// a backtick so that PowerShell does not expand it.
std::string BundledReadmeText() {
    const std::string script = ReadFile("scripts/package_release.ps1");
    const std::string begin = "$readmeContent = @\"";
    const size_t start = script.find(begin);
    const size_t end = script.find("\n\"@", start);
    if (start == std::string::npos || end == std::string::npos) return "";
    std::string text = script.substr(start + begin.size(), end - start - begin.size());
    for (size_t at = text.find("`$"); at != std::string::npos; at = text.find("`$", at + 1)) text.erase(at, 1);
    return text;
}

// The first capture group of the first match, or "" when there is none.
std::string FirstMatch(const std::string& text, const std::string& pattern) {
    std::smatch match;
    return std::regex_search(text, match, std::regex(pattern)) ? match[1].str() : "";
}

unsigned long Hex(const std::string& digits) {
    return digits.empty() ? ~0ul : std::stoul(digits, nullptr, 16);
}

// What the Installer script writes for the game's exe (the constants of its [Code] section).
struct InstallerDumpValues {
    std::string type, flagsHex, count;
};
InstallerDumpValues WhatTheInstallerWrites() {
    const std::string iss = ReadFile("installer/fs25_vr.iss");
    return {FirstMatch(iss, R"(CrashDumpType\s*=\s*(\d+);)"), FirstMatch(iss, R"(CrashDumpFlags\s*=\s*\$([0-9A-Fa-f]+);)"),
            FirstMatch(iss, R"(CrashDumpCount\s*=\s*(\d+);)")};
}

std::string CountInWords(const std::string& count) {
    static const char* const words[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
    const int n = count.empty() ? -1 : std::stoi(count);
    return n >= 0 && n <= 10 ? words[n] : "(no word for " + count + ")";
}

TEST(CrashDumpConsistencyTest, TheInstallersCustomDumpFlagsAreTheContentsOfTheInjectorsOwnDump) {
    const InstallerDumpValues installer = WhatTheInstallerWrites();
    ASSERT_FALSE(installer.flagsHex.empty()) << "installer/fs25_vr.iss has no CrashDumpFlags constant.";

    // The Injector's dump contents, as the flags it passes to MiniDumpWriteDump.
    EXPECT_EQ(Hex(installer.flagsHex), fs25vr::kCrashDumpFlags);
    EXPECT_EQ(installer.type, "0") << "DumpType 0 is the custom dump, the only one that reads CustomDumpFlags.";

    // The other places the flags are written down.
    EXPECT_EQ(Hex(FirstMatch(ReadFile("docs/adr/0039-the-installer-turns-on-windows-crash-dumps-for-the-game.md"),
                             R"(`CustomDumpFlags` 0x([0-9A-Fa-f]+))")),
              fs25vr::kCrashDumpFlags)
        << "ADR-0039, item 1";
    const std::string installerTest = ReadFile("scripts/test_installer.ps1");
    EXPECT_EQ(Hex(FirstMatch(installerTest, R"re(\$expected = \[ordered\]@\{[^}]*CustomDumpFlags = 0x([0-9A-Fa-f]+);)re")),
              fs25vr::kCrashDumpFlags)
        << "scripts/test_installer.ps1, the key it expects";
    EXPECT_EQ(FirstMatch(installerTest, R"re(WrittenCustomDumpFlags"\] -eq "(\d+)")re"), std::to_string(fs25vr::kCrashDumpFlags))
        << "scripts/test_installer.ps1, the record it expects";
}

TEST(CrashDumpConsistencyTest, TheInstallerWritesTheDumpCountTheReadmeTheAdrAndTheWizardState) {
    const InstallerDumpValues installer = WhatTheInstallerWrites();
    ASSERT_FALSE(installer.count.empty()) << "installer/fs25_vr.iss has no CrashDumpCount constant.";
    const std::string words = CountInWords(installer.count);

    EXPECT_NE(ReadFile("README.md").find("Windows keeps the last " + words), std::string::npos) << "README.md says how many dumps Windows keeps";
    EXPECT_NE(BundledReadmeText().find("Windows keeps the last " + words), std::string::npos) << "the README.txt in the bundle";
    EXPECT_NE(ReadFile("installer/fs25_vr.iss").find("keeps the last " + installer.count + " dumps"), std::string::npos)
        << "the Crash Dumps page of the wizard";
    EXPECT_EQ(FirstMatch(ReadFile("docs/adr/0039-the-installer-turns-on-windows-crash-dumps-for-the-game.md"), R"(`DumpCount` (\d+))"),
              installer.count)
        << "ADR-0039, item 1";
    EXPECT_EQ(FirstMatch(ReadFile("scripts/test_installer.ps1"), R"re(\$expected = \[ordered\]@\{[^}]*DumpCount = (\d+) \})re"),
              installer.count)
        << "scripts/test_installer.ps1, the key it expects";
}

TEST(CrashDumpConsistencyTest, TheCommandThatTurnsTheSettingOnWritesWhatTheInstallerWrites) {
    const InstallerDumpValues installer = WhatTheInstallerWrites();
    const std::vector<std::string> commands = CommandLinesIn(ReadFile("README.md"));
    ASSERT_FALSE(commands.empty());

    EXPECT_NE(commands.front().find("@{DumpType=" + installer.type + "; CustomDumpFlags=0x" + installer.flagsHex +
                                    "; DumpCount=" + installer.count + "}"),
              std::string::npos)
        << commands.front();
}

TEST(CrashDumpConsistencyTest, TheReadmeAndTheBundledReadmeGiveTheSameCrashCommands) {
    const std::vector<std::string> readme = CommandLinesIn(ReadFile("README.md"));
    const std::vector<std::string> bundled = CommandLinesIn(BundledReadmeText());

    // The one that turns the setting on and the one that turns it off, both aimed at the game's key.
    ASSERT_EQ(readme.size(), 2u) << "README.md should hold exactly the command that turns the setting on and the one that turns it off.";
    for (const std::string& command : readme) {
        EXPECT_NE(command.find("Windows Error Reporting\\LocalDumps"), std::string::npos) << command;
        EXPECT_NE(command.find("FarmingSimulator2025Game.exe"), std::string::npos) << command;
    }
    EXPECT_EQ(bundled, readme) << "The README.txt in the bundle (scripts/package_release.ps1) and README.md must give the same commands.";
}

} // namespace
