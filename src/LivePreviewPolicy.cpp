#include "LivePreviewPolicy.h"

namespace LivePreviewPolicy {

bool shouldUpdateLiveFullFramePreview(StartupPhase phase, bool frameLooksLikeHardwareRoi)
{
    if (frameLooksLikeHardwareRoi) {
        return false;
    }

    return phase == StartupPhase::LocatePair ||
           phase == StartupPhase::Tracking;
}

bool shouldShowLocalizationFrameBeforeStarSelection(StartupPhase phase,
                                                    bool frameLooksLikeHardwareRoi)
{
    return phase == StartupPhase::LocatePair && !frameLooksLikeHardwareRoi;
}

} // namespace LivePreviewPolicy
