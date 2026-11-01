// Click Points and Coupling Points share the pointer cone (ADR-0035, spec #227, ticket #231), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game, Interactive Control and Manual Attach stood in by Lua stubs (manual_attach_test_support.h). Of a
// Click Point and a hitch both within the pointer cone, the one nearer the ray's centre is the target, either way round; the other selects nothing.
// Prior art: test_manual_attach_pointer.cpp (a Click Point and a hitch exactly on the ray's centre, where the Click Point wins the tie).

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

}  // namespace

TEST_F(VirtualGamepadManualAttachTest, AHitchNearerTheRaysCentreThanAClickPointIsTheTargetAndTheClickPointSelectsNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);  // the rear hitch is on the ray's centre
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.37, -0.2, 1.0)");  // 3 cm off the centre, 0.7 m ahead: about 2.5 degrees
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the ray ends on the hitch, which is nearer its centre";
    EXPECT_EQ(EvalLua(L, "tostring(StubIC.selected())"), "nil") << "the Click Point is not clickable";

    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1) << "the pull works the hitch";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "and clicks nothing";
}
