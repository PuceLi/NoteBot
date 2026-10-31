#include "NBSDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace notebot {
namespace {

std::string normalizeTextEncoding(std::string text) {
    if (text.empty() || text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return text;
    const int inputLength = static_cast<int>(text.size());
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputLength, nullptr, 0) > 0)
        return text;

    const int wideLength = MultiByteToWideChar(CP_ACP, 0, text.data(), inputLength, nullptr, 0);
    if (wideLength <= 0) return {};
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text.data(), inputLength, wide.data(), wideLength) != wideLength)
        return {};

    const int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
                                                nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return {};
    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
                            utf8.data(), utf8Length, nullptr, nullptr) != utf8Length)
        return {};
    return utf8;
}

} // namespace

std::optional<Song> NBSDecoder::decode(const std::filesystem::path& filePath) {
    return decodeWithError(filePath).song;
}

NBSDecoder::DecodeResult NBSDecoder::decodeWithError(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return {std::nullopt, "Could not open the file (check that it exists and is not in use)"};
    }

    try {
        Song   song;
        int    nbsVersion = 0;
        uint8_t firstCustomInstrument = 16;
        const uint16_t length = readShort(file);

        if (length == 0) {
            nbsVersion = readByte(file);
            if (nbsVersion < 1 || nbsVersion > 6) {
                return {std::nullopt, "Unsupported NBS version " + std::to_string(nbsVersion) +
                    " (supported versions: 1-6)"};
            }
            firstCustomInstrument = readByte(file);
            if (firstCustomInstrument < 16) {
                return {std::nullopt, "Invalid first custom instrument ID " +
                    std::to_string(firstCustomInstrument)};
            }
            if (nbsVersion >= 3) { readShort(file); }
        }

        readShort(file);
        song.title  = readString(file, nbsVersion);
        if (song.title.find_first_not_of(" \t\r\n") == std::string::npos) {
            const auto filename = filePath.stem().u8string();
            song.title.assign(reinterpret_cast<const char*>(filename.data()), filename.size());
        }
        song.author = readString(file, nbsVersion);
        readString(file, nbsVersion);
        readString(file, nbsVersion);

        const float speed = readShort(file) / 100.0f;
        if (speed <= 0.0f) {
            return {std::nullopt, "Invalid song speed (the tempo value is zero)"};
        }
        readByte(file);
        readByte(file);
        readByte(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readInt(file);
        readString(file, nbsVersion);

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
                roundedTick > std::numeric_limits<int>::max()) {
                return {std::nullopt, "Song timing is out of range near tick " +
                    std::to_string(tick)};
            }

            while (true) {
                const uint16_t jumpLayers = readShort(file);
                if (jumpLayers == 0) break;

                const uint8_t instrument = readByte(file);
                const uint8_t key = readByte(file);

                if (key > 87) {
                    return {std::nullopt, "Invalid note key " + std::to_string(key) +
                        " at tick " + std::to_string(static_cast<int>(roundedTick)) +
                        " (valid NBS keys are 0-87)"};
                }

                if (nbsVersion >= 4) {
                    readByte(file);
                    readByte(file);
                    readShort(file);
                }

                const bool isCustomInstrument = instrument >= firstCustomInstrument;
                const Instrument inst = isCustomInstrument
                    ? static_cast<Instrument>(instrument)
                    : fromNBSInstrument(instrument);
                Note note(inst, key - NOTE_OFFSET, isCustomInstrument, instrument);
                song.notesMap.insert({static_cast<int>(roundedTick), note});
            }
        }

        song.finishLoading();
        return {std::move(song), {}};

    } catch (const std::exception& exception) {
        auto position = file.tellg();
        if (position == std::streampos(-1)) {
            file.clear();
            file.seekg(0, std::ios::end);
            position = file.tellg();
        }
        std::string error = exception.what();
        if (position != std::streampos(-1)) {
            error += " at byte " + std::to_string(static_cast<std::streamoff>(position));
        }
        return {std::nullopt, std::move(error)};
    } catch (...) {
        return {std::nullopt, "Unknown error while reading the NBS file"};
    }
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

std::string NBSDecoder::readString(std::ifstream& stream, int nbsVersion) {
    const int32_t length = readInt(stream);
    constexpr int32_t maxStringLength = 1024 * 1024;
    if (length < 0 || length > maxStringLength) {
        throw std::runtime_error("Invalid NBS string length");
    }

    std::string result;
    if (nbsVersion >= 6) {
        // NBS v6 stores the number of UTF-8 characters, not the byte count.
        result.reserve(static_cast<size_t>(length));
        for (int32_t character = 0; character < length; ++character) {
            const uint8_t first = readByte(stream);
            int continuationCount = 0;
            uint32_t codepoint = 0;
            if (first <= 0x7F) {
                codepoint = first;
            } else if (first >= 0xC2 && first <= 0xDF) {
                continuationCount = 1;
                codepoint = first & 0x1F;
            } else if (first >= 0xE0 && first <= 0xEF) {
                continuationCount = 2;
                codepoint = first & 0x0F;
            } else if (first >= 0xF0 && first <= 0xF4) {
                continuationCount = 3;
                codepoint = first & 0x07;
            } else {
                throw std::runtime_error("Invalid UTF-8 in NBS v6 string");
            }

            result.push_back(static_cast<char>(first));
            for (int index = 0; index < continuationCount; ++index) {
                const uint8_t continuation = readByte(stream);
                if ((continuation & 0xC0) != 0x80) {
                    throw std::runtime_error("Invalid UTF-8 in NBS v6 string");
                }
                result.push_back(static_cast<char>(continuation));
                codepoint = (codepoint << 6) | (continuation & 0x3F);
            }
            if ((continuationCount == 1 && codepoint < 0x80) ||
                (continuationCount == 2 && codepoint < 0x800) ||
                (continuationCount == 3 && codepoint < 0x10000) ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
                throw std::runtime_error("Invalid UTF-8 in NBS v6 string");
            }
        }
    } else {
        const auto start = stream.tellg();
        stream.seekg(0, std::ios::end);
        const auto end = stream.tellg();
        if (start == std::streampos(-1) || end == std::streampos(-1) || end - start < length) {
            throw std::runtime_error("Truncated NBS string");
        }
        stream.seekg(start);

        result.resize(static_cast<size_t>(length));
        if (length > 0 && !stream.read(result.data(), length)) {
            throw std::runtime_error("Could not read NBS string");
        }
        result = normalizeTextEncoding(std::move(result));
    }
    std::replace(result.begin(), result.end(), '\r', ' ');
    return result;
}

Instrument NBSDecoder::fromNBSInstrument(uint8_t instrument) {
    return nbsInstrument(instrument);
}

} // namespace notebot
