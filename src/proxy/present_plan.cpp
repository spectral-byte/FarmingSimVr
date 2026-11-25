#include "proxy/present_plan.h"

namespace fs25vr {

namespace {

PresentPlan PresentAsTheGameDid() {
    PresentPlan plan;
    plan.latchQueuedMode = true;
    return plan;
}

void ApplyDesktopMirror(const PresentFacts& facts, PresentPlan& plan) {
    if (!facts.desktopMirrorOff) {
        return;
    }
    if (facts.mirrorWasShowing) {
        plan.clearBackBufferToBlack = true;
        plan.mirror = MirrorBookkeeping::Off;
    } else {
        plan.callRealPresent = false;
        plan.mirror = MirrorBookkeeping::Unchanged;
    }
}

}

PresentPlan PlanPresent(const PresentFacts& facts) {
    if (!facts.active || !facts.eyeSwapchains) {
        return PresentAsTheGameDid();
    }

    if (!facts.sessionRunning) {
        PresentPlan plan = PresentAsTheGameDid();
        plan.resetPacing = true;
        plan.inQuadModeAfter = false;
        return plan;
    }

    PresentPlan plan;
    const bool inQuadMode = !facts.companionActive || facts.menuVisible || facts.floatingWindow2D;
    if (inQuadMode) {
        plan.copyToEye = 0;
        plan.alignMenuQuad = !facts.wasInQuadMode;
        plan.submit = EyeSubmission::MenuQuad;
        plan.latchQueuedMode = true;
        plan.inQuadModeAfter = true;
        ApplyDesktopMirror(facts, plan);
        return plan;
    }

    plan.resetPacing = facts.wasInQuadMode;
    plan.presentWasStereo = true;
    plan.inQuadModeAfter = false;
    plan.hideMenuCursor = true;
    plan.snapshotEye = true;
    const uint32_t eye = facts.wasInQuadMode ? 0 : facts.eyePhase;
    plan.copyToEye = eye;

    if (facts.sequentialDualPass && eye == 0) {
        plan.advance = FrameAdvance::ToSecondSubPass;
        plan.callRealPresent = false;
        plan.mirror = MirrorBookkeeping::Unchanged;
        return plan;
    }

    plan.advance = FrameAdvance::ToNextFrame;
    if (eye == 1) {
        plan.submit = EyeSubmission::FramePair;
        plan.latchQueuedMode = true;
    }
    ApplyDesktopMirror(facts, plan);
    return plan;
}

}
