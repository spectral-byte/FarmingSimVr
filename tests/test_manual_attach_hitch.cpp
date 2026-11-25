#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

}

TEST_F(VirtualGamepadManualAttachTest, PullingTheTriggerWithTheRayOnAHitchAttachesTheLinedUpImplementAndASecondPullDetachesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(StubChunk("manual_attach_tractor_and_implement"));
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
