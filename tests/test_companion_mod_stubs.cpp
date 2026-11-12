// The engine stubs are .luau files under tests/stubs/, read from disk by name when a test asks for one (spec #280,
// ticket #287), so a contributor edits a stub as Lua and reruns the tests without a rebuild.

#include <gtest/gtest-spi.h>
#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

// Asks for a stub that is not there. Static and self-contained so that EXPECT_NONFATAL_FAILURE can run it.
void LoadAMissingStub() {
    StubChunk("no_such_stub");
}

}  // namespace

TEST_F(CompanionModTest, AStubLoadedByNameRunsAsLua) {
    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);
    EXPECT_EQ(EvalLua(L, "type(StubScene.newNode)"), "function");
}

TEST_F(CompanionModTest, AStubThatCannotBeFoundFailsTheTestNamingTheFile) {
    EXPECT_NONFATAL_FAILURE(LoadAMissingStub(), "tests/stubs/no_such_stub.luau");
}

TEST_F(CompanionModTest, AStubThatCannotBeFoundRaisesWhenRun) {
    std::string chunk;
    EXPECT_NONFATAL_FAILURE(chunk = StubChunk("no_such_stub"), "no_such_stub");
    EXPECT_NE(RunLuauChunk(L, chunk, "=missing"), 0);
}
