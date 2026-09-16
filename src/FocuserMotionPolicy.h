#pragma once

inline bool isFocuserMotionAllowed(bool globalMotionAllowed,
                                   bool manualMotionAllowed,
                                   bool automaticOperation)
{
    return globalMotionAllowed && (automaticOperation || manualMotionAllowed);
}
