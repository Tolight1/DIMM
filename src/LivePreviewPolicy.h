#pragma once

namespace LivePreviewPolicy {

enum class StartupPhase {
    None,
    LocatePair,
    Tracking
};

bool shouldUpdateLiveFullFramePreview(StartupPhase phase, bool frameLooksLikeHardwareRoi);
bool shouldShowLocalizationFrameBeforeStarSelection(StartupPhase phase,
                                                    bool frameLooksLikeHardwareRoi);

} // namespace LivePreviewPolicy
