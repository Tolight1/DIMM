#pragma once

class SearchEventGate final
{
public:
    bool tryBeginSearch()
    {
        if (m_searchEntryRecorded) {
            return false;
        }

        m_searchEntryRecorded = true;
        return true;
    }

    void markTracking()
    {
        m_searchEntryRecorded = false;
    }

    void reset()
    {
        m_searchEntryRecorded = false;
    }

private:
    bool m_searchEntryRecorded = false;
};
