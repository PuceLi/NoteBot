#include "NBSDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace notebot {

std::optional<Song> NBSDecoder::decode(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) { return std::nullopt; }

    try {
        Song   song;
        int    nbsVersion = 0;
        const uint16_t length = readShort(file);

        if (length == 0) {
            nbsVersion = readByte(file);
            if (nbsVersion < 1 || nbsVersion > 5) return std::nullopt;
            readByte(file);
            if (nbsVersion >= 3) { readShort(file); }
        }

        readShort(file);
        song.title  = readString(file);
        if (song.title.find_first_not_of(" \t\r\n") == std::string::npos) {
            const auto filename = filePath.stem().u8string();
            song.title.assign(reinterpret_cast<const char*>(filename.data()), filename.size());
        }
        song.author = readString(file);
        readString(file);
        readString(file);

        const float speed = readShort(file) / 100.0f;
        if (speed <= 0.0f) return std::nullopt;
        readByte(file);
        readByte(file);
        readByte(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readString(file);

        if (nbsVersion >= 4) {
            readByte(file);
            readByte(file);
            readShort(file);
        }

        double tick = -1;
        while (true) {
            const uint16_t jumpTicks = readShort(file);
            if (jumpTicks == 0) break;

            tick += jumpTicks * (20.0f / speed);
            const double roundedTick = std::round(tick);
            if (!std::isfinite(roundedTick) || roundedTick < std::numeric_limits<int>::min() ||
                roundedTick > std::numeric_limits<int>::max()) return std::nullopt;

            while (true) {
                const uint16_t jumpLayers = readShort(file);
                if (jumpLayers == 0) break;

                const uint8_t instrument = readByte(file);
                const uint8_t key = readByte(file);

                if (nbsVersion >= 4) {
                    readByte(file);
                    readByte(file);
                    readShort(file);
                }

                Instrument inst = fromNBSInstrument(instrument);
                Note       note(inst, key - NOTE_OFFSET);
                song.notesMap.insert({static_cast<int>(roundedTick), note});
            }
        }

        song.finishLoading();
        return song;

    } catch (...) { return std::nullopt; }
}

uint8_t NBSDecoder::readByte(std::ifstream& stream) {
    const int value = stream.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error("Unexpected end of NBS file");
    }
    return static_cast<uint8_t>(value);
}

uint16_t NBSDecoder::readShort(std::ifstream& stream) {
    const uint16_t byte1 = readByte(stream);
    const uint16_t byte2 = readByte(stream);
    return static_cast<uint16_t>(byte1 | (byte2 << 8));
}

int32_t NBSDecoder::readInt(std::ifstream& stream) {
    const uint32_t byte1 = readByte(stream);
    const uint32_t byte2 = readByte(stream);
    const uint32_t byte3 = readByte(stream);
    const uint32_t byte4 = readByte(stream);
    return static_cast<int32_t>(byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
}

std::string NBSDecoder::readString(std::ifstream& stream) {
    const int32_t length = readInt(stream);
    constexpr int32_t maxStringLength = 1024 * 1024;
    if (length < 0 || length > maxStringLength) {
        throw std::runtime_error("Invalid NBS string length");
    }
    const auto start = stream.tellg();
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (start == std::streampos(-1) || end == std::streampos(-1) || end - start < length) {
        throw std::runtime_error("Truncated NBS string");
    }
    stream.seekg(start);

    std::string result(static_cast<size_t>(length), '\0');
    if (length > 0 && !stream.read(result.data(), length)) {
        throw std::runtime_error("Could not read NBS string");
    }
    std::replace(result.begin(), result.end(), '\r', ' ');
    return result;
}

Instrument NBSDecoder::fromNBSInstrument(uint8_t instrument) {
    if (instrument <= 15) { return static_cast<Instrument>(instrument); }
    return Instrument::Harp;
}

} // namespace notebot
