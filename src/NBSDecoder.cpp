#include "NBSDecoder.h"

namespace notebot {

std::optional<Song> NBSDecoder::decode(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) { return std::nullopt; }

    try {
        Song   song;
        int    nbsVersion = 0;
        int16_t length     = readShort(file);

        if (length == 0) {
            nbsVersion = file.get();
            file.get();
            if (nbsVersion >= 3) { length = readShort(file); }
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

        float speed = readShort(file) / 100.0f;
        file.get();
        file.get();
        file.get();
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readString(file);

        if (nbsVersion >= 4) {
            file.get();
            file.get();
            readShort(file);
        }

        double tick = -1;
        while (true) {
            int16_t jumpTicks = readShort(file);
            if (jumpTicks == 0) break;

            tick += jumpTicks * (20.0f / speed);

            while (true) {
                int16_t jumpLayers = readShort(file);
                if (jumpLayers == 0) break;

                uint8_t instrument = file.get();
                int8_t  key        = file.get();

                if (nbsVersion >= 4) {
                    file.get();
                    file.get();
                    readShort(file);
                }

                Instrument inst = fromNBSInstrument(instrument);
                Note       note(inst, key - NOTE_OFFSET);
                song.notesMap.insert({static_cast<int>(std::round(tick)), note});
            }
        }

        song.finishLoading();
        return song;

    } catch (...) { return std::nullopt; }
}

int16_t NBSDecoder::readShort(std::ifstream& stream) {
    uint8_t byte1 = stream.get();
    uint8_t byte2 = stream.get();
    return static_cast<int16_t>(byte1 | (byte2 << 8));
}

int32_t NBSDecoder::readInt(std::ifstream& stream) {
    uint8_t byte1 = stream.get();
    uint8_t byte2 = stream.get();
    uint8_t byte3 = stream.get();
    uint8_t byte4 = stream.get();
    return static_cast<int32_t>(byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
}

std::string NBSDecoder::readString(std::ifstream& stream) {
    int32_t length = readInt(stream);
    if (length <= 0) return "";

    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        char c = stream.get();
        if (c == '\r') c = ' ';
        result += c;
    }
    return result;
}

Instrument NBSDecoder::fromNBSInstrument(uint8_t instrument) {
    if (instrument <= 15) { return static_cast<Instrument>(instrument); }
    return Instrument::Harp;
}

} // namespace notebot
