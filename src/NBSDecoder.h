#pragma once

#include "NoteData.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace notebot {

class NBSDecoder {
public:
    static constexpr int NOTE_OFFSET = 33;

    struct DecodeResult {
        std::optional<Song> song;
        std::string error;
    };

    static std::optional<Song> decode(const std::filesystem::path& filePath);
    static DecodeResult decodeWithError(const std::filesystem::path& filePath);

private:
    static uint8_t  readByte(std::ifstream& stream);
    static uint16_t readShort(std::ifstream& stream);
    static int32_t  readInt(std::ifstream& stream);
    static std::string readString(std::ifstream& stream, int nbsVersion);
    static Instrument fromNBSInstrument(uint8_t instrument);
};

} // namespace notebot
