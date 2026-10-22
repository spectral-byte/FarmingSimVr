#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct lua_State;

// Companion Mod harness: the module files are the only implementation, loaded in modDesc order.
// CompanionModLoadOrder() must mirror <extraSourceFiles> in modDesc.xml (guarded by a test).
inline const std::vector<std::string>& CompanionModLoadOrder() {
    static const std::vector<std::string> order = {
        "src/VRCompanionConfig.lua",
        "src/VRCompanionCore.lua",
        "src/VRCompanionStereo.lua",
        "src/VRCompanionLocomotion.lua",
        "src/VRCompanionSeatOffset.lua",
        "src/VRCompanionHud.lua",
        "src/VRCompanionInput.lua",
        "src/VRCompanionMotionControllers.lua",
        "src/VRCompanionMenuPointer.lua",
        "src/VRCompanionVirtualHands.lua",
        "src/VRCompanionWheelGrab.lua",
        "src/VRCompanionVirtualGamepad.lua",
        "src/VRCompanionHandTools.lua",
        "src/VRCompanionObjectGrab.lua",
        "src/VRCompanionDialogProbe.lua",
        "src/VRCompanionFishCatch.lua",
        "src/VRCompanionInteractiveControl.lua",
        "src/VRCompanion.lua",
    };
    return order;
}

inline constexpr const char* kCompanionModDir = "mods/FS25_VRCompanion/";

// Extracts <sourceFile filename="..."/> entries (in document order) from modDesc.xml content.
std::vector<std::string> ParseModDescSourceFiles(const std::string& modDescXml);

// Resolves a repo-relative path against cwd, PROJECT_ROOT_DIR, or parent dir. Empty if not found.
std::filesystem::path ResolveProjectPath(const std::string& relativePath);

// Loads every Companion Mod file in CompanionModLoadOrder() into L, exactly as the engine does.
// Returns 0 on success; otherwise a non-zero status with the error pushed on the stack and in errorOut.
int LoadCompanionMod(lua_State* L, std::string* errorOut = nullptr);

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
    static int LuauLoadCompanionMod(lua_State* L);
    static int LuauTracebackHandler(lua_State* L);
};
