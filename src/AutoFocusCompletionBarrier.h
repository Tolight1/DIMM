#pragma once

#include <cstdint>

class AutoFocusCompletionBarrier final {
public:
    void begin(int cameraIndex)
    {
        const std::uint8_t bit = cameraBit(cameraIndex);
        m_participants |= bit;
        m_completed &= static_cast<std::uint8_t>(~bit);
    }

    void complete(int cameraIndex)
    {
        m_completed |= static_cast<std::uint8_t>(m_participants & cameraBit(cameraIndex));
    }

    void reset()
    {
        m_participants = 0;
        m_completed = 0;
    }

    bool active() const
    {
        return m_participants != 0;
    }

    std::uint8_t takeReadyMask()
    {
        if (!active() || m_completed != m_participants) {
            return 0;
        }
        const std::uint8_t participants = m_participants;
        reset();
        return participants;
    }

private:
    static std::uint8_t cameraBit(int cameraIndex)
    {
        return cameraIndex >= 0 && cameraIndex < 2
                   ? static_cast<std::uint8_t>(1U << cameraIndex)
                   : 0;
    }

    std::uint8_t m_participants = 0;
    std::uint8_t m_completed = 0;
};
