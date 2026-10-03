#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct lua_State;

struct LuauExecutionResult {
    bool success = false;
    int exitCode = 0;
    std::string errorMessage;
    std::string traceback;
    std::vector<std::string> capturedLogs;
};

class LuauTestRunner {
public:
    LuauTestRunner();
    ~LuauTestRunner();

    LuauTestRunner(const LuauTestRunner&) = delete;
    LuauTestRunner& operator=(const LuauTestRunner&) = delete;

    lua_State* GetState() const { return L_; }

    LuauExecutionResult ExecuteCode(const std::string& code, const std::string& chunkName = "=(test)");
    LuauExecutionResult ExecuteFile(const std::string& relativePath);

    std::filesystem::path ResolvePath(const std::string& relativePath) const;
    std::string ReadFile(const std::string& relativePath) const;

private:
    lua_State* L_ = nullptr;
    std::vector<std::string> capturedLogs_;

    void InitializeEnvironment();
    static int LuauPrint(lua_State* L);
    static int LuauLoadString(lua_State* L);
    static int LuauRequire(lua_State* L);
    static int LuauFsReadFile(lua_State* L);
    static int LuauTracebackHandler(lua_State* L);
};
