// Manual Attach, working the PTO stub and the hose couplers with the Pointer Ray (ADR-0035, spec #227, ticket #229), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game and Manual Attach stood in by Lua stubs
// (manual_attach_test_support.h). The PTO stub and the hose couplers are the vehicle's side of an Implement's hitch; the player points at one
// and pulls the trigger, and Manual Attach's own PTO or hose path runs for the Implement on that hitch. Covered here: what is offered and when,
// which Coupling Point of a hitch wins, that each pull runs exactly one path, the pulses, the Virtual Gamepad, and the releases of Manual Attach
// the support gives up on. Prior art: test_manual_attach_hitch.cpp and test_manual_attach_pointer.cpp.

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

}  // namespace

// ---- The PTO stub ---------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, PullingTheTriggerWithTheRayOnThePtoStubConnectsThePtoAndASecondPullDisconnectsIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearPtoStub) << "the ray ends on the vehicle's PTO stub";
    ASSERT_EQ(Performs(), 0);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("pto"), 1) << "one pull runs Manual Attach's PTO attachment once";
    EXPECT_EQ(PtoConnected("plow"), "true");
    ReleaseTrigger(kRight);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("pto"), 2);
    EXPECT_EQ(PtoConnected("plow"), "false") << "the second pull disconnects it";
}
