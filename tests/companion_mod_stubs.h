#pragma once

#include <gtest/gtest.h>
#include "luau_test_runner.h"
#include <fstream>
#include <sstream>
#include <string>

inline std::string StubPath(const std::string& name) { return "tests/stubs/" + name + ".luau"; }

// The Luau text of tests/stubs/<name>.luau, found from the working directories the tests support (ResolveProjectPath).
// A stub that cannot be found or read fails the test with a message naming the file, and its chunk raises when run.
inline std::string StubChunk(const std::string& name) {
    const std::string path = StubPath(name);
    const auto resolved = ResolveProjectPath(path);
    std::ifstream file;
    if (!resolved.empty()) file.open(resolved);
    if (!file.is_open()) {
        ADD_FAILURE() << "engine stub not found: " << path;
        return "error('engine stub not found: " + path + "')\n";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline std::string StubInstalled(const std::string& name, const std::string& install) {
    return StubChunk(name) + "\n" + install + "\n";
}
