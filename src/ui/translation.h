#pragma once

#include "ll/api/i18n/I18n.h"

#include <string>
#include <string_view>

namespace notebot::ui {

inline std::string translate(std::string_view key, std::string_view fallback) {
    const auto& i18n = ll::i18n::getInstance();
    const std::string_view locale = ll::i18n::getDefaultLocaleCode();
    auto value = i18n.get(key, locale);
    if (value.empty() || value == key) {
        const auto separator = locale.find_first_of("-_");
        if (separator != std::string_view::npos)
            value = i18n.get(key, locale.substr(0, separator));
    }
    if (value.empty() || value == key) value = i18n.get(key, "en");
    return value.empty() || value == key ? std::string(fallback) : std::string(value);
}

} // namespace notebot::ui
