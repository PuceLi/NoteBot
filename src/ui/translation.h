#pragma once

#include "ll/api/i18n/I18n.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"

#include <fmt/format.h>
#include <string>
#include <string_view>
#include <utility>

namespace notebot::ui {

inline std::string_view minecraftLocaleCode() {
    const auto client = ll::service::getClientInstance();
    if (!client) return "en_US";
    const auto language = client->getOptions().getLanguage();
    if (language == "zh" || language.starts_with("zh_") || language.starts_with("zh-"))
        return "zh_CN";
    return "en_US";
}

inline std::string translate(std::string_view key, std::string_view fallback) {
    const auto& i18n = ll::i18n::getInstance();
    auto value = i18n.get(key, minecraftLocaleCode());
    if (value.empty() || value == key) value = i18n.get(key, "en_US");
    return value.empty() || value == key ? std::string(fallback) : std::string(value);
}

template <typename... Args>
inline std::string format(std::string_view key, std::string_view fallback, Args&&... args) {
    return fmt::format(fmt::runtime(translate(key, fallback)), std::forward<Args>(args)...);
}

} // namespace notebot::ui
