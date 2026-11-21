#include "UiAppearance.h"
#include <nlohmann/json.hpp>

namespace notebot {

std::string serializeUiAppearance(const UiAppearance& appearance) {
    return nlohmann::json{{"format", "NoteBot UI"}, {"version", 1}, {"scale", appearance.scale},
        {"titleAlignment", appearance.titleAlignment}, {"language", appearance.language},
        {"helpText", appearance.helpText}, {"hideHud", appearance.hideHud}, {"colors", appearance.colors},
        {"rainbowColors", appearance.rainbowColors}}.dump(2);
}

bool deserializeUiAppearance(std::string_view text, UiAppearance& appearance) {
    if (text.empty() || text.size() > 64 * 1024) return false;
    try {
        const auto json = nlohmann::json::parse(text);
        if (!json.is_object() || json.at("format") != "NoteBot UI" || json.at("version") != 1) return false;
        UiAppearance candidate;
        if (!json.at("scale").is_number() || !json.at("titleAlignment").is_number_integer() ||
            !json.at("language").is_number_integer() || !json.at("version").is_number_integer()) return false;
        candidate.scale = json.at("scale").get<float>();
        candidate.titleAlignment = json.at("titleAlignment").get<int>();
        candidate.language = json.at("language").get<int>();
        candidate.helpText = json.at("helpText").get<bool>();
        candidate.hideHud = json.at("hideHud").get<bool>();
        const auto& colors = json.at("colors");
        if (!colors.is_array() || (colors.size() != candidate.colors.size() && colors.size() != 21)) return false;
        if (const auto rainbow = json.find("rainbowColors"); rainbow != json.end()) {
            if (!rainbow->is_array() || rainbow->size() != colors.size()) return false;
            for (size_t i = 0; i < rainbow->size(); ++i)
                candidate.rainbowColors[i] = rainbow->at(i).get<bool>();
        }
        for (size_t i = 0; i < colors.size(); ++i) {
            if (!colors[i].is_array() || colors[i].size() != 4) return false;
            for (size_t channel = 0; channel < 4; ++channel) {
                if (!colors[i][channel].is_number()) return false;
                candidate.colors[i][channel] = colors[i][channel].get<float>();
            }
        }
        if (sanitizeUiAppearance(candidate)) return false;
        appearance = candidate;
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

} // namespace notebot
