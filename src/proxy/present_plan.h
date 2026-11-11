#pragma once

#include <cstdint>
#include <optional>

namespace fs25vr {

// What one Present of the game's swap chain is, decided in one step from facts alone (ADR-0015, ADR-0019). The DxgiProxy
// gathers the facts, asks PlanPresent for the plan, carries it out in a fixed order and does the bookkeeping once, so a
// case cannot forget one of the bookkeeping steps every case shares.

// The facts of one Present, read after the runtime's events were polled and the eye swapchains were looked after.
struct PresentFacts {
    // The Injector is Active (not Uninitialized or in Pass-Through Mode), after the events were polled.
    bool active = false;
    bool eyeSwapchains = false;       // the OpenXR manager has eye swapchains
    bool sessionRunning = false;      // the headset session accepts frames
    bool companionActive = false;     // the Companion Mod's handshake has completed
    bool menuVisible = false;         // a menu is showing
    bool floatingWindow2D = false;    // 2D Floating Window Mode is on
    bool sequentialDualPass = false;  // the Stereo Presentation Mode: Sequential Dual-Pass, else Alternate Frame Rendering
    // The eye this Present would render: the sub-pass under Sequential Dual-Pass, the frame parity under Alternate Frame
    // Rendering. As it stands now, before any pacing reset the plan asks for.
    uint32_t eyePhase = 0;
    bool wasInQuadMode = false;     // the previous Present put the picture on the menu quad
    bool desktopMirrorOff = false;  // the Desktop Mirror Window is off right now
    bool mirrorWasShowing = false;  // ... and was showing at the previous Present that decided it
};

enum class EyeSubmission {
    None,
    MenuQuad,   // the menu quad is submitted as a completed compositor frame
    FramePair,  // the left and right eye images are submitted as one frame
};

// Which counter moves on after the copy and the submission.
enum class FrameAdvance {
    None,
    ToSecondSubPass,  // Sequential Dual-Pass: the next Present is sub-pass 1
    ToNextFrame,      // the rendered frame advances; the sub-pass and parity follow it
};

// What the Present leaves in "the Desktop Mirror Window was showing".
enum class MirrorBookkeeping {
    Unchanged,
    Showing,
    Off,
};

struct PresentPlan {
    // The eye that gets the back buffer, if any.
    std::optional<uint32_t> copyToEye;
    // Back to eye 0 / sub-pass 0 before anything else: a fresh eye pair starts.
    bool resetPacing = false;
    // The mouse cursor drawn on the menu quad goes, if it was showing.
    bool hideMenuCursor = false;
    // `vrEyeSnapshot` may write the eye just copied.
    bool snapshotEye = false;
    EyeSubmission submit = EyeSubmission::None;
    // With the menu quad: the quad is placed in front of the head first (the picture just moved onto it).
    bool alignMenuQuad = false;
    FrameAdvance advance = FrameAdvance::None;
    // A queued Stereo Presentation Mode may be applied: no eye pair is in flight.
    bool latchQueuedMode = false;
    // The back buffer goes black, after the eye copy, before the real Present: the Desktop Mirror Window's last frame.
    bool clearBackBufferToBlack = false;
    // The game's real Present is called; otherwise it is suppressed.
    bool callRealPresent = true;
    MirrorBookkeeping mirror = MirrorBookkeeping::Showing;
    // This Present is a stereo presentation: it advances the rendered-frame counter, any other drops the camera records.
    bool presentWasStereo = false;
    // Whether the next Present finds the picture on the menu quad; empty leaves it as it was.
    std::optional<bool> inQuadModeAfter;
};

// No side effects: the same facts give the same plan.
PresentPlan PlanPresent(const PresentFacts& facts);

}  // namespace fs25vr
