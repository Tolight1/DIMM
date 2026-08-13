#pragma once

#include <algorithm>
#include <cstdlib>

namespace ConnectedDomain {

constexpr int kFourConnectivity = 4;
constexpr int kEightConnectivity = 8;
constexpr int kDefaultConnectivity = kEightConnectivity;
constexpr int kMinimumComponentArea = 9;

inline int sanitizeConnectivity(int connectivity)
{
    return connectivity == kFourConnectivity ? kFourConnectivity : kEightConnectivity;
}

inline bool isNeighborOffset(int dx,
                             int dy,
                             int connectivity = kDefaultConnectivity)
{
    if (sanitizeConnectivity(connectivity) == kFourConnectivity) {
        return std::abs(dx) + std::abs(dy) == 1;
    }
    return std::max(std::abs(dx), std::abs(dy)) == 1;
}

} // namespace ConnectedDomain
