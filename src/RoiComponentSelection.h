#pragma once

#include <cmath>
#include <limits>
#include <vector>

namespace RoiComponentSelection {

struct ComponentCandidate {
    int label = 0;
    int area = 0;
    double localX = 0.0;
    double localY = 0.0;
};

struct PreviousGlobalCentroid {
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
};

enum class SelectionFailure {
    None,
    NoValidComponents,
    MissingPreviousGlobalCentroid
};

struct SelectionResult {
    bool selected = false;
    int label = 0;
    double localX = 0.0;
    double localY = 0.0;
    SelectionFailure failure = SelectionFailure::NoValidComponents;
};

inline SelectionResult selectTargetComponent(const std::vector<ComponentCandidate>& candidates,
                                             int minArea,
                                             int roiX,
                                             int roiY,
                                             const PreviousGlobalCentroid& previous)
{
    std::vector<ComponentCandidate> valid;
    const int safeMinArea = minArea < 1 ? 1 : minArea;
    for (const ComponentCandidate& candidate : candidates) {
        if (candidate.area >= safeMinArea) {
            valid.push_back(candidate);
        }
    }

    SelectionResult result;
    if (valid.empty()) {
        result.failure = SelectionFailure::NoValidComponents;
        return result;
    }

    if (valid.size() == 1) {
        result.selected = true;
        result.label = valid.front().label;
        result.localX = valid.front().localX;
        result.localY = valid.front().localY;
        result.failure = SelectionFailure::None;
        return result;
    }

    if (!previous.valid) {
        result.failure = SelectionFailure::MissingPreviousGlobalCentroid;
        return result;
    }

    double bestDistance2 = std::numeric_limits<double>::max();
    const ComponentCandidate* best = nullptr;
    for (const ComponentCandidate& candidate : valid) {
        const double globalX = static_cast<double>(roiX) + candidate.localX;
        const double globalY = static_cast<double>(roiY) + candidate.localY;
        const double dx = globalX - previous.x;
        const double dy = globalY - previous.y;
        const double distance2 = dx * dx + dy * dy;
        if (distance2 < bestDistance2) {
            bestDistance2 = distance2;
            best = &candidate;
        }
    }

    if (best) {
        result.selected = true;
        result.label = best->label;
        result.localX = best->localX;
        result.localY = best->localY;
        result.failure = SelectionFailure::None;
    }
    return result;
}

} // namespace RoiComponentSelection
