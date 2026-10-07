#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace notebot {

enum class Instrument : uint8_t {
    Harp          = 0,
    Bass          = 1,
    BaseDrum      = 2,
    Snare         = 3,
    Hat           = 4,
    Guitar        = 5,
    Flute         = 6,
    Bell          = 7,
    Chime         = 8,
    Xylophone     = 9,
    IronXylophone = 10,
    CowBell       = 11,
    Didgeridoo    = 12,
    Bit           = 13,
    Banjo         = 14,
    Pling         = 15,
};

struct Note {
    Instrument instrument;
    int8_t     noteLevel;

    Note(Instrument inst, int8_t level) : instrument(inst), noteLevel(level) {}

    bool operator==(const Note& other) const {
        return instrument == other.instrument && noteLevel == other.noteLevel;
    }
};

struct Song {
    std::multimap<int, Note> notesMap;
    int                      lastTick = 0;
    std::string              title;
    std::string              author;
    bool                     finishedLoading = false;

    void finishLoading() {
        if (finishedLoading) return;
        if (!notesMap.empty()) { lastTick = notesMap.rbegin()->first; }
        finishedLoading = true;
    }
};

} // namespace notebot
