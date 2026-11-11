#include "proxy/present_plan.h"

namespace fs25vr {

namespace {

// A Present that does no VR work: the game presents as it always did, no eye pair is in flight (so a queued Stereo
// Presentation Mode may be applied) and the Desktop Mirror Window shows by itself.
PresentPlan PresentAsTheGameDid() {
    PresentPlan plan;
    plan.latchQueuedMode = true;
    return plan;  // callRealPresent and mirror keep their defaults: present, mirror Showing
}

// With the headset fed, the Desktop Mirror Window decides whether the real Present is called. A mirror going from
// showing to off presents once more, black, so the window does not keep the last frame. A mirror that stays on keeps
// the plan's defaults: present, mirror Showing.
void ApplyDesktopMirror(const PresentFacts& facts, PresentPlan& plan) {
    if (!facts.desktopMirrorOff) {
        return;
    }
    if (facts.mirrorWasShowing) {
        plan.clearBackBufferToBlack = true;
        plan.mirror = MirrorBookkeeping::Off;  // still presents (the default), once, black
    } else {
        plan.callRealPresent = false;
        plan.mirror = MirrorBookkeeping::Unchanged;
    }
}

}  // namespace

PresentPlan PlanPresent(const PresentFacts& facts) {
    // Not Active, or no eye swapchains (creation failed): no eye pair can be in flight.
    if (!facts.active || !facts.eyeSwapchains) {
        return PresentAsTheGameDid();
    }

    if (!facts.sessionRunning) {
        // IDLE or STOPPING (headset asleep, dashboard) or not yet READY: no VR frames, the game keeps presenting to the
        // Desktop Mirror Window. On resume a fresh eye pair starts and the menu quad is aligned to the new head pose.
        PresentPlan plan = PresentAsTheGameDid();
        plan.resetPacing = true;
        plan.inQuadModeAfter = false;
        return plan;
    }

    PresentPlan plan;
    const bool inQuadMode = !facts.companionActive || facts.menuVisible || facts.floatingWindow2D;
    if (inQuadMode) {
        // Pre-game, a menu or 2D Floating Window Mode: the back buffer goes on the floating quad layer, a completed
        // compositor frame of its own.
        plan.copyToEye = 0;
        plan.alignMenuQuad = !facts.wasInQuadMode;
        plan.submit = EyeSubmission::MenuQuad;
        plan.latchQueuedMode = true;
        plan.inQuadModeAfter = true;
        ApplyDesktopMirror(facts, plan);
        return plan;
    }

    // Stereoscopic 3D view. Coming back from the quad starts the eye pair over, so the phase is that of a fresh one.
    plan.resetPacing = facts.wasInQuadMode;
    plan.presentWasStereo = true;
    plan.inQuadModeAfter = false;
    plan.hideMenuCursor = true;
    plan.snapshotEye = true;
    const uint32_t eye = facts.wasInQuadMode ? 0 : facts.eyePhase;
    plan.copyToEye = eye;

    if (facts.sequentialDualPass && eye == 0) {
        // Sub-Pass 0: the left eye, the physical Present is suppressed, the next Present is sub-pass 1.
        plan.advance = FrameAdvance::ToSecondSubPass;
        plan.callRealPresent = false;
        plan.mirror = MirrorBookkeeping::Unchanged;
        return plan;
    }

    // Sub-Pass 1, or Alternate Frame Rendering: the pair is whole after the right eye.
    plan.advance = FrameAdvance::ToNextFrame;
    if (eye == 1) {
        plan.submit = EyeSubmission::FramePair;
        plan.latchQueuedMode = true;
    }
    ApplyDesktopMirror(facts, plan);
    return plan;
}

}  // namespace fs25vr
