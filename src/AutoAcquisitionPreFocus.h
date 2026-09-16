#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

struct TemperatureFocusTarget {
    std::array<double, 2> position{};
    bool extrapolated = false;
    double selectedTemperatureC = 0.0;
};

inline std::optional<TemperatureFocusTarget> calculateTemperatureFocusTarget(double temperatureC)
{
    if (!std::isfinite(temperatureC)) {
        return std::nullopt;
    }

    struct TableRow {
        int temperatureC;
        int position1;
        int position2;
    };
    static constexpr std::array<TableRow, 46> kTable = {{
        {-20, 27536, 30201}, {-19, 27526, 30191}, {-18, 27516, 30181},
        {-17, 27506, 30171}, {-16, 27495, 30162}, {-15, 27485, 30152},
        {-14, 27475, 30142}, {-13, 27465, 30132}, {-12, 27454, 30122},
        {-11, 27444, 30112}, {-10, 27434, 30102}, { -9, 27424, 30092},
        { -8, 27414, 30082}, { -7, 27403, 30072}, { -6, 27393, 30062},
        { -5, 27383, 30052}, { -4, 27373, 30042}, { -3, 27362, 30032},
        { -2, 27352, 30023}, { -1, 27342, 30013}, {  0, 27332, 30003},
        {  1, 27322, 29993}, {  2, 27311, 29983}, {  3, 27301, 29973},
        {  4, 27291, 29963}, {  5, 27281, 29953}, {  6, 27270, 29943},
        {  7, 27260, 29933}, {  8, 27250, 29923}, {  9, 27240, 29913},
        { 10, 27230, 29903}, { 11, 27219, 29893}, { 12, 27209, 29884},
        { 13, 27199, 29874}, { 14, 27189, 29864}, { 15, 27178, 29854},
        { 16, 27168, 29844}, { 17, 27158, 29834}, { 18, 27148, 29824},
        { 19, 27137, 29814}, { 20, 27127, 29804}, { 21, 27117, 29794},
        { 22, 27107, 29784}, { 23, 27097, 29774}, { 24, 27086, 29764},
        { 25, 27076, 29754},
    }};

    if (temperatureC >= kTable.front().temperatureC && temperatureC <= kTable.back().temperatureC) {
        const TableRow* closest = &kTable.front();
        for (const TableRow& row : kTable) {
            if (std::abs(temperatureC - row.temperatureC) <
                std::abs(temperatureC - closest->temperatureC)) {
                closest = &row;
            }
        }
        return TemperatureFocusTarget{{static_cast<double>(closest->position1),
                                       static_cast<double>(closest->position2)},
                                      false,
                                      static_cast<double>(closest->temperatureC)};
    }

    const double position1 = -10.22560694 * temperatureC + 27331.76868;
    const double position2 = -9.928653177 * temperatureC + 30002.7105;
    if (!std::isfinite(position1) || !std::isfinite(position2)) {
        return std::nullopt;
    }
    return TemperatureFocusTarget{{position1, position2}, true, temperatureC};
}

inline std::optional<int> clampAndRoundFocuserPosition(double position, int maxStep)
{
    if (!std::isfinite(position) || maxStep <= 0) {
        return std::nullopt;
    }
    return static_cast<int>(std::round(std::clamp(position, 0.0, static_cast<double>(maxStep))));
}

struct AutoAcquisitionPreFocusDeviceState {
    bool opened = false;
    bool positionValid = false;
    bool motionValid = false;
    bool moving = false;
    int position = 0;
    int maxStep = 0;
};

enum class AutoAcquisitionPreFocusActionType {
    None,
    MoveAbsolute
};

struct AutoAcquisitionPreFocusAction {
    AutoAcquisitionPreFocusActionType type = AutoAcquisitionPreFocusActionType::None;
    int target = 0;
};

class AutoAcquisitionPreFocus final {
public:
    bool begin(double temperatureC)
    {
        const auto target = calculateTemperatureFocusTarget(temperatureC);
        if (!target) {
            return false;
        }
        m_target = *target;
        m_active = true;
        m_readyTaken = false;
        m_stage.fill(Stage::AwaitingOpen);
        m_sentTarget.fill(0);
        return true;
    }

    void cancel()
    {
        m_active = false;
        m_readyTaken = false;
        m_stage.fill(Stage::Inactive);
    }

    bool active() const { return m_active; }
    bool blocksAutoFocus() const { return m_active && !allSlotsFinished(); }

    AutoAcquisitionPreFocusAction update(int cameraIndex,
                                         const AutoAcquisitionPreFocusDeviceState& state)
    {
        if (!m_active || cameraIndex < 0 || cameraIndex >= static_cast<int>(m_stage.size())) {
            return {};
        }

        Stage& stage = m_stage[cameraIndex];
        if (stage == Stage::AwaitingOpen) {
            if (!state.opened) {
                return {};
            }
            return advanceWhenIdle(cameraIndex, state);
        }
        if (stage == Stage::AwaitingIdle) {
            return advanceWhenIdle(cameraIndex, state);
        }
        if (stage == Stage::AwaitingStop) {
            finishIfStopped(cameraIndex, state, true);
        } else if (stage == Stage::AwaitingFailedStop) {
            finishIfStopped(cameraIndex, state, false);
        }
        return {};
    }

    void markMoveAccepted(int cameraIndex)
    {
        if (isCameraWaitingFor(cameraIndex, Stage::AwaitingMoveAcknowledgement)) {
            m_stage[cameraIndex] = Stage::AwaitingStop;
        }
    }

    void markMoveFailed(int cameraIndex)
    {
        if (isCameraWaitingFor(cameraIndex, Stage::AwaitingMoveAcknowledgement)) {
            m_stage[cameraIndex] = Stage::AwaitingFailedStop;
        }
    }

    void markOpenFailed(int cameraIndex)
    {
        if (m_active && cameraIndex >= 0 && cameraIndex < static_cast<int>(m_stage.size()) &&
            m_stage[cameraIndex] == Stage::AwaitingOpen) {
            m_stage[cameraIndex] = Stage::Skipped;
        }
    }

    bool awaitingMoveAcknowledgement(int cameraIndex) const
    {
        return isCameraWaitingFor(cameraIndex, Stage::AwaitingMoveAcknowledgement);
    }

    bool takeReady()
    {
        if (!m_active || !allSlotsFinished() || m_readyTaken) {
            return false;
        }
        m_readyTaken = true;
        m_active = false;
        return true;
    }

private:
    enum class Stage {
        Inactive,
        AwaitingOpen,
        AwaitingIdle,
        AwaitingMoveAcknowledgement,
        AwaitingStop,
        AwaitingFailedStop,
        Completed,
        Skipped
    };

    AutoAcquisitionPreFocusAction advanceWhenIdle(int cameraIndex,
                                                   const AutoAcquisitionPreFocusDeviceState& state)
    {
        if (!state.positionValid || !state.motionValid || state.maxStep <= 0) {
            return {};
        }
        const auto target = clampAndRoundFocuserPosition(m_target.position[cameraIndex], state.maxStep);
        if (!target) {
            return {};
        }
        m_sentTarget[cameraIndex] = *target;
        if (state.moving) {
            m_stage[cameraIndex] = Stage::AwaitingIdle;
            return {};
        }
        if (state.position == *target) {
            m_stage[cameraIndex] = Stage::Completed;
            return {};
        }
        m_stage[cameraIndex] = Stage::AwaitingMoveAcknowledgement;
        return {AutoAcquisitionPreFocusActionType::MoveAbsolute, *target};
    }

    void finishIfStopped(int cameraIndex,
                         const AutoAcquisitionPreFocusDeviceState& state,
                         bool requireTarget)
    {
        if (!state.opened || !state.positionValid || !state.motionValid || state.moving) {
            return;
        }
        m_stage[cameraIndex] = requireTarget && state.position == m_sentTarget[cameraIndex]
                                   ? Stage::Completed
                                   : Stage::Skipped;
    }

    bool allSlotsFinished() const
    {
        for (const Stage stage : m_stage) {
            if (stage != Stage::Completed && stage != Stage::Skipped) {
                return false;
            }
        }
        return true;
    }

    bool isCameraWaitingFor(int cameraIndex, Stage stage) const
    {
        return m_active && cameraIndex >= 0 && cameraIndex < static_cast<int>(m_stage.size()) &&
               m_stage[cameraIndex] == stage;
    }

    TemperatureFocusTarget m_target;
    std::array<Stage, 2> m_stage{{Stage::Inactive, Stage::Inactive}};
    std::array<int, 2> m_sentTarget{};
    bool m_active = false;
    bool m_readyTaken = false;
};
