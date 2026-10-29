#include "luau_test_runner.h"
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <string_view>

LuauTestRunner::LuauTestRunner() {
    InitializeEnvironment();
}

LuauTestRunner::~LuauTestRunner() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

// Luau's base library only has gcinfo. collectgarbage serves weak-table tests (a forced full collection, the default)
// and allocation tests: "collect", "count" (KB, with the byte remainder), "stop" and "restart".
static int LuauCollectGarbage(lua_State* L) {
    std::string_view option = luaL_optstring(L, 1, "collect");
    if (option == "collect") {
        lua_gc(L, LUA_GCCOLLECT, 0);
        return 0;
    }
    if (option == "count") {
        lua_pushnumber(L, lua_gc(L, LUA_GCCOUNT, 0) + lua_gc(L, LUA_GCCOUNTB, 0) / 1024.0);
        return 1;
    }
    if (option == "stop") {
        lua_gc(L, LUA_GCSTOP, 0);
        return 0;
    }
    if (option == "restart") {
        lua_gc(L, LUA_GCRESTART, 0);
        return 0;
    }
    luaL_error(L, "collectgarbage: unsupported option '%s'", option.data());
    return 0;
}

void LuauTestRunner::InitializeEnvironment() {
    L_ = luaL_newstate();
    if (!L_) return;

    luaL_openlibs(L_);

    // Register print to capture logs
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, &LuauPrint, "print", 1);
    lua_setglobal(L_, "print");

    lua_pushcfunction(L_, &LuauCollectGarbage, "collectgarbage");
    lua_setglobal(L_, "collectgarbage");

    // Register loadstring
    lua_pushcfunction(L_, &LuauLoadString, "loadstring");
    lua_setglobal(L_, "loadstring");

    // Register loadCompanionMod (loads module files in modDesc order, as the engine does)
    lua_pushcfunction(L_, &LuauLoadCompanionMod, "loadCompanionMod");
    lua_setglobal(L_, "loadCompanionMod");

    // Register require (supporting @lune/fs)
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, &LuauRequire, "require", 1);
    lua_setglobal(L_, "require");
}

int LuauTestRunner::LuauPrint(lua_State* L) {
    auto* runner = static_cast<LuauTestRunner*>(lua_touserdata(L, lua_upvalueindex(1)));
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) line += "\t";
        const char* s = lua_tostring(L, i);
        line += (s ? s : "nil");
    }
    if (runner) {
        runner->capturedLogs_.push_back(line);
    }
    return 0;
}

int LuauTestRunner::LuauLoadString(lua_State* L) {
    size_t len = 0;
    const char* str = luaL_checklstring(L, 1, &len);
    const char* chunkName = luaL_optstring(L, 2, "=(loadstring)");

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(str, len, nullptr, &bytecodeSize);
    if (!bytecode) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to compile chunk (allocation failure)");
        return 2;
    }

    int loadResult = luau_load(L, chunkName, bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loadResult != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

int LuauTestRunner::LuauRequire(lua_State* L) {
    auto* runner = static_cast<LuauTestRunner*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* mod = luaL_checkstring(L, 1);
    if (std::string_view(mod) == "@lune/fs") {
        lua_newtable(L);
        lua_pushlightuserdata(L, runner);
        lua_pushcclosure(L, &LuauFsReadFile, "readFile", 1);
        lua_setfield(L, -2, "readFile");
        return 1;
    }
    luaL_error(L, "module '%s' not found", mod);
    return 0;
}

int LuauTestRunner::LuauFsReadFile(lua_State* L) {
    auto* runner = static_cast<LuauTestRunner*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* path = luaL_checkstring(L, 1);
    if (!runner) {
        luaL_error(L, "LuauTestRunner not available");
        return 0;
    }
    auto resolved = runner->ResolvePath(path);
    if (resolved.empty() || !std::filesystem::exists(resolved)) {
        luaL_error(L, "File not found: %s", path);
        return 0;
    }
    std::string content = runner->ReadFile(path);
    lua_pushlstring(L, content.data(), content.size());
    return 1;
}

int LuauTestRunner::LuauTracebackHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "traceback");
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, 1);
            lua_pushinteger(L, 2);
            if (lua_pcall(L, 2, 1, 0) == 0) {
                return 1;
            }
        }
    }
    lua_pushstring(L, msg ? msg : "unknown error");
    return 1;
}

std::filesystem::path ResolveProjectPath(const std::string& relativePath) {
    std::filesystem::path p(relativePath);
    if (std::filesystem::exists(p)) {
        return p;
    }
#ifdef PROJECT_ROOT_DIR
    auto rootP = std::filesystem::path(PROJECT_ROOT_DIR) / relativePath;
    if (std::filesystem::exists(rootP)) {
        return rootP;
    }
#endif
    auto parentP = std::filesystem::path("..") / relativePath;
    if (std::filesystem::exists(parentP)) {
        return parentP;
    }
    return {};
}

std::filesystem::path LuauTestRunner::ResolvePath(const std::string& relativePath) const {
    return ResolveProjectPath(relativePath);
}

std::vector<std::string> ParseModDescSourceFiles(const std::string& modDescXml) {
    std::vector<std::string> files;
    static const std::regex re(R"re(<sourceFile\s+filename\s*=\s*"([^"]+)")re");
    for (auto it = std::sregex_iterator(modDescXml.begin(), modDescXml.end(), re);
         it != std::sregex_iterator(); ++it) {
        files.push_back((*it)[1].str());
    }
    return files;
}

int LoadCompanionMod(lua_State* L, std::string* errorOut) {
    auto fail = [&](int status, const std::string& msg) {
        if (errorOut) *errorOut = msg;
        lua_pushstring(L, msg.c_str());
        return status;
    };
    // Resolve the mod root once so every file comes from the same checkout
    const std::string modDescRel = std::string(kCompanionModDir) + "modDesc.xml";
    const auto modDescPath = ResolveProjectPath(modDescRel);
    if (modDescPath.empty()) return fail(-1, "Companion Mod not found: " + modDescRel);
    const auto modRoot = modDescPath.parent_path();
    for (const auto& rel : CompanionModLoadOrder()) {
        const std::string repoRel = std::string(kCompanionModDir) + rel;
        const auto path = modRoot / rel;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return fail(-1, "Companion Mod file not readable: " + repoRel);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string code = buffer.str();
        if (code.empty()) return fail(-1, "Companion Mod file is empty: " + repoRel);

        size_t bytecodeSize = 0;
        char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
        if (!bytecode) return fail(-1, "luau_compile failed for " + repoRel);
        const std::string chunkName = "=" + path.filename().string();
        int loadResult = luau_load(L, chunkName.c_str(), bytecode, bytecodeSize, 0);
        free(bytecode);
        if (loadResult != 0) {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
            lua_pop(L, 1);
            return fail(loadResult, repoRel + ": " + err);
        }
        int status = lua_pcall(L, 0, 0, 0);
        if (status != 0) {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "runtime error";
            lua_pop(L, 1);
            return fail(status, repoRel + ": " + err);
        }
    }
    return 0;
}

int LuauTestRunner::LuauLoadCompanionMod(lua_State* L) {
    std::string err;
    if (LoadCompanionMod(L, &err) != 0) {
        lua_pop(L, 1);
        luaL_error(L, "loadCompanionMod failed: %s", err.c_str());
    }
    return 0;
}
std::string LuauTestRunner::ReadFile(const std::string& relativePath) const {
    auto path = ResolvePath(relativePath);
    if (path.empty()) return "";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

LuauExecutionResult LuauTestRunner::ExecuteCode(const std::string& code, const std::string& chunkName) {
    capturedLogs_.clear();

    if (!L_) {
        return LuauExecutionResult{
            .success = false,
            .errorMessage = "Lua state not initialized"
        };
    }

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
    if (!bytecode) {
        return LuauExecutionResult{
            .success = false,
            .exitCode = -1,
            .errorMessage = "luau_compile failed to allocate memory",
            .capturedLogs = capturedLogs_
        };
    }

    lua_pushcfunction(L_, &LuauTracebackHandler, "traceback_handler");
    int errFuncIdx = lua_gettop(L_);

    int loadResult = luau_load(L_, chunkName.c_str(), bytecode, bytecodeSize, 0);
    free(bytecode);

    if (loadResult != 0) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Failed to load chunk";
        lua_pop(L_, 2);
        return LuauExecutionResult{
            .success = false,
            .exitCode = loadResult,
            .errorMessage = err,
            .capturedLogs = capturedLogs_
        };
    }

    int status = lua_pcall(L_, 0, 0, errFuncIdx);
    if (status != 0) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "Runtime error";
        lua_pop(L_, 2);
        std::string msg = err;
        std::string tb;
        auto newlinePos = err.find('\n');
        if (newlinePos != std::string::npos) {
            msg = err.substr(0, newlinePos);
            tb = err.substr(newlinePos + 1);
        }
        return LuauExecutionResult{
            .success = false,
            .exitCode = status,
            .errorMessage = msg,
            .traceback = tb,
            .capturedLogs = capturedLogs_
        };
    }

    lua_pop(L_, 1);

    bool hasFailures = false;
    for (const auto& log : capturedLogs_) {
        if (log.starts_with("  [FAIL] ") || log.starts_with("[FAIL] ")) {
            hasFailures = true;
            break;
        }
    }

    return LuauExecutionResult{
        .success = !hasFailures,
        .exitCode = hasFailures ? 1 : 0,
        .errorMessage = hasFailures ? "One or more assertions failed in captured test output" : "",
        .capturedLogs = capturedLogs_
    };
}

LuauExecutionResult LuauTestRunner::ExecuteFile(const std::string& relativePath) {
    auto path = ResolvePath(relativePath);
    if (path.empty() || !std::filesystem::exists(path)) {
        return LuauExecutionResult{
            .success = false,
            .errorMessage = "File not found: " + relativePath,
            .capturedLogs = capturedLogs_
        };
    }
    std::string code = ReadFile(relativePath);
    std::string chunkName = "=" + path.filename().string();
    return ExecuteCode(code, chunkName);
}
