#include "NBSDecoder.h"
#include "ui/translation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace notebot {
namespace {

constexpr std::string_view replacementCharacter = "\xEF\xBF\xBD";

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

NBSDecoder::DecodeResult NBSDecoder::decodeWithError(const std::filesystem::path& filePath, bool preview) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return {std::nullopt, ui::translate("notebot.decoder.file_open",
            "Could not open the file (check that it exists and is not in use)")};
    }

    try {
        Song   song;
        int    nbsVersion = 0;
        uint8_t firstCustomInstrument = 16;
        const uint16_t length = readShort(file);

        if (length == 0) {
            nbsVersion = readByte(file);
            if (nbsVersion < 1 || nbsVersion > 6) {
                return {std::nullopt, ui::format("notebot.decoder.unsupported_version",
                    "Unsupported NBS version {0} (supported versions: 1-6)", nbsVersion)};
            }
            firstCustomInstrument = readByte(file);
            if (firstCustomInstrument < 16) {
                return {std::nullopt, ui::format("notebot.decoder.invalid_custom_id",
                    "Invalid first custom instrument ID {0}", firstCustomInstrument)};
            }
            if (nbsVersion >= 3) { readShort(file); }
        }

        const uint16_t layerCount = readShort(file);
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
            return {std::nullopt, ui::translate("notebot.decoder.invalid_speed",
                "Invalid song speed (the tempo value is zero)")};
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
                return {std::nullopt, ui::format("notebot.decoder.timing_out_of_range",
                    "Song timing is out of range near tick {0}", tick)};
            }

            int layer = -1;
            while (true) {
                const uint16_t jumpLayers = readShort(file);
                if (jumpLayers == 0) break;
                if (preview) {
                    if (layer > std::numeric_limits<int>::max() - jumpLayers)
                        throw std::runtime_error("NBS layer index is out of range");
                    layer += jumpLayers;
                }

                const uint8_t instrument = readByte(file);
                const uint8_t key = readByte(file);

                if (key > 87) {
                    return {std::nullopt, ui::format("notebot.decoder.invalid_note_key",
                        "Invalid note key {0} at tick {1} (valid NBS keys are 0-87)",
                        key, static_cast<int>(roundedTick))};
                }

                float pan = 0.0f;
                if (nbsVersion >= 4) {
                    readByte(file);
                    // NBS stores panning as 0 (left), 100 (centre), 200 (right).
                    pan = (std::clamp(static_cast<int>(readByte(file)), 0, 200) - 100) / 100.0f;
                    readShort(file);
                }

                const bool isCustomInstrument = instrument >= firstCustomInstrument;
                const Instrument inst = isCustomInstrument
                    ? static_cast<Instrument>(instrument)
                    : fromNBSInstrument(instrument);
                Note note(inst, key - NOTE_OFFSET, isCustomInstrument, instrument);
                if (preview) {
                    note.sourceLayer = layer;
                    note.previewPan = pan;
                }
                song.notesMap.insert({static_cast<int>(roundedTick), note});
            }
        }

        if (preview) {
            // Read additional audio metadata only for preview.
            std::vector<float> authoredLayerPans(layerCount, 0.0f);
            if (file.peek() != std::char_traits<char>::eof()) {
                for (auto& layerPan : authoredLayerPans) {
                    readString(file, nbsVersion);
                    if (nbsVersion >= 4) readByte(file); // Layer lock.
                    readByte(file); // Layer volume.
                    if (nbsVersion >= 2)
                        layerPan = (std::clamp(static_cast<int>(readByte(file)), 0, 200) - 100) / 100.0f;
                }
            }
            for (auto& [noteTick, note] : song.notesMap) {
                if (static_cast<size_t>(note.sourceLayer) < authoredLayerPans.size())
                    note.previewPan = std::clamp(note.previewPan + authoredLayerPans[note.sourceLayer], -1.0f, 1.0f);
            }

            std::map<int, float> layerPans;
            bool hasAuthoredPan = false;
            for (const auto& [noteTick, note] : song.notesMap) {
                if (note.customInstrument) continue;
                layerPans.try_emplace(note.sourceLayer, 0.0f);
                hasAuthoredPan = hasAuthoredPan || note.previewPan != 0.0f;
            }
            if (!hasAuthoredPan && layerPans.size() > 1) {
                size_t index = 0;
                for (auto& [layer, pan] : layerPans)
                    pan = -0.65f + 1.3f * static_cast<float>(index++) / static_cast<float>(layerPans.size() - 1);
                for (auto& [noteTick, note] : song.notesMap) {
                    if (!note.customInstrument) note.previewPan = layerPans.at(note.sourceLayer);
                }
            } else if (!hasAuthoredPan && !layerPans.empty()) {
                int minPitch = std::numeric_limits<int>::max();
                int maxPitch = std::numeric_limits<int>::min();
                for (const auto& [noteTick, note] : song.notesMap) {
                    if (note.customInstrument) continue;
                    minPitch = std::min(minPitch, static_cast<int>(note.noteLevel));
                    maxPitch = std::max(maxPitch, static_cast<int>(note.noteLevel));
                }
                if (maxPitch > minPitch) {
                    for (auto& [noteTick, note] : song.notesMap) {
                        if (note.customInstrument) continue;
                        const float position = static_cast<float>(note.noteLevel - minPitch) /
                            static_cast<float>(maxPitch - minPitch);
                        note.previewPan = -0.55f + position * 1.1f;
                    }
                }
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
            error = ui::format("notebot.decoder.exception_at_byte", "{0} at byte {1}",
                error, static_cast<std::streamoff>(position));
        }
        return {std::nullopt, std::move(error)};
    } catch (...) {
        return {std::nullopt, ui::translate("notebot.decoder.unknown_error",
            "Unknown error while reading the NBS file")};
    }
}

uint8_t NBSDecoder::readByte(std::ifstream& stream) {
    const int value = stream.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error(ui::translate("notebot.decoder.unexpected_end",
            "Unexpected end of NBS file"));
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
        throw std::runtime_error(ui::translate("notebot.decoder.invalid_string_length",
            "Invalid NBS string length"));
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
                // Bad metadata must not prevent the note data from being used.
                // Consume one declared character and emit valid UTF-8 for ImGui.
                result.append(replacementCharacter);
                continue;
            }

            std::array<uint8_t, 3> continuationBytes{};
            int consumedContinuations = 0;
            bool valid = true;
            for (int index = 0; index < continuationCount; ++index) {
                const uint8_t continuation = readByte(stream);
                if ((continuation & 0xC0) != 0x80) {
                    // This byte may be the start of the next character. Put it
                    // back so the declared character count remains aligned.
                    stream.clear();
                    stream.seekg(-1, std::ios::cur);
                    valid = false;
                    break;
                }
                continuationBytes[static_cast<size_t>(index)] = continuation;
                ++consumedContinuations;
                codepoint = (codepoint << 6) | (continuation & 0x3F);
            }

            if ((continuationCount == 1 && codepoint < 0x80) ||
                (continuationCount == 2 && codepoint < 0x800) ||
                (continuationCount == 3 && codepoint < 0x10000) ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF ||
                (first == 0xE0 && continuationBytes[0] < 0xA0) ||
                (first == 0xED && continuationBytes[0] >= 0xA0) ||
                (first == 0xF0 && continuationBytes[0] < 0x90) ||
                (first == 0xF4 && continuationBytes[0] >= 0x90)) {
                valid = false;
            }

            if (!valid) {
                result.append(replacementCharacter);
                continue;
            }

            result.push_back(static_cast<char>(first));
            for (int index = 0; index < consumedContinuations; ++index)
                result.push_back(static_cast<char>(continuationBytes[static_cast<size_t>(index)]));
        }
    } else {
        const auto start = stream.tellg();
        stream.seekg(0, std::ios::end);
        const auto end = stream.tellg();
        if (start == std::streampos(-1) || end == std::streampos(-1) || end - start < length) {
            throw std::runtime_error(ui::translate("notebot.decoder.truncated_string",
                "Truncated NBS string"));
        }
        stream.seekg(start);

        result.resize(static_cast<size_t>(length));
        if (length > 0 && !stream.read(result.data(), length)) {
            throw std::runtime_error(ui::translate("notebot.decoder.read_string",
                "Could not read NBS string"));
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
