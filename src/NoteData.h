#pragma once

#include "mc/deps/shared_types/v1_26_20/block/NoteBlockInstrument.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace notebot {

using Instrument = SharedTypes::v1_26_20::NoteBlockInstrument;

inline constexpr size_t kMappedInstrumentCount =
    static_cast<size_t>(Instrument::TrumpetOxidized) + 1;
using NoteMap = std::array<int, kMappedInstrumentCount>;

inline constexpr Instrument nbsInstrument(uint8_t instrument) {
    switch (instrument) {
    case 0: return Instrument::Harp;
    case 1: return Instrument::Bass;
    case 2: return Instrument::BassDrum;
    case 3: return Instrument::Snare;
    case 4: return Instrument::Hat;
    case 5: return Instrument::Guitar;
    case 6: return Instrument::Flute;
    case 7: return Instrument::Bell;
    case 8: return Instrument::Chime;
    case 9: return Instrument::Xylophone;
    case 10: return Instrument::IronXylophone;
    case 11: return Instrument::CowBell;
    case 12: return Instrument::Didgeridoo;
    case 13: return Instrument::Bit;
    case 14: return Instrument::Banjo;
    case 15: return Instrument::Pling;
    default: return static_cast<Instrument>(instrument);
    }
}

inline constexpr NoteMap makeDefaultNoteMap() {
    NoteMap mapping{};
    for (size_t index = 0; index < mapping.size(); ++index) {
        mapping[index] = static_cast<int>(nbsInstrument(static_cast<uint8_t>(index))) + 1;
    }
    return mapping;
}

inline Instrument mapInstrument(uint8_t sourceInstrumentId, Instrument source, const NoteMap& mapping) {
    if (sourceInstrumentId >= mapping.size()) return source;
    const int mappedInstrument = mapping[sourceInstrumentId] - 1;
    if (mappedInstrument < 0 || mappedInstrument >= static_cast<int>(kMappedInstrumentCount)) return source;
    return static_cast<Instrument>(mappedInstrument);
}

struct Note {
    Instrument instrument;
    int8_t     noteLevel;
    bool       customInstrument;
    uint8_t    sourceInstrumentId;

    Note(Instrument inst, int8_t level, bool isCustom = false, uint8_t sourceId = 0)
    : instrument(inst), noteLevel(level), customInstrument(isCustom), sourceInstrumentId(sourceId) {}

    bool operator==(const Note& other) const {
        return instrument == other.instrument && noteLevel == other.noteLevel &&
            customInstrument == other.customInstrument && sourceInstrumentId == other.sourceInstrumentId;
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
