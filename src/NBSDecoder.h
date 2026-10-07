#pragma once

#include "NoteData.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include <optional>

namespace notebot {

class NBSDecoder {
public:
    static constexpr int NOTE_OFFSET = 33;

    static std::optional<Song> decode(const std::filesystem::path& filePath);

private:
    static int16_t  readShort(std::ifstream& stream);
    static int32_t  readInt(std::ifstream& stream);
    static std::string readString(std::ifstream& stream);
    static Instrument fromNBSInstrument(uint8_t instrument);
};

} // namespace notebot
