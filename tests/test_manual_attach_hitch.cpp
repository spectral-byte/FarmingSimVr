// Manual Attach, working a hitch with the Pointer Ray (ADR-0035, spec #227, ticket #228), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game and Manual Attach stood in by Lua stubs (manual_attach_test_support.h). The hitch is the
// attacher joint of a vehicle; the player points at it and pulls the trigger, and Manual Attach's own attach or detach runs for it. Covered here:
// which hitches can be pointed at (within Manual Attach's reach, with an Implement lined up or on them) and which one is worked, what Manual Attach
// refuses, and the ray that ends on the hitch. Prior art: test_animated_objects_pointer.cpp.

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

}  // namespace

TEST_F(VirtualGamepadManualAttachTest, PullingTheTriggerWithTheRayOnAHitchAttachesTheLinedUpImplementAndASecondPullDetachesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the ray ends on the hitch";
    ASSERT_EQ(Performs(), 0);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1) << "one pull runs Manual Attach's joint attachment once";
    EXPECT_EQ(Attached("plow"), "true") << "and attaches the lined-up Implement";
    ReleaseTrigger(kRight);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 2);
    EXPECT_EQ(Attached("plow"), "false") << "the second pull detaches it";
}
