#pragma once

namespace notebot {

class ImGuiManager {
public:
    static ImGuiManager& getInstance();

    bool initialize();
    void shutdown();
    bool isInitialized() const { return mInitialized; }

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;

    bool mInitialized = false;
};

} // namespace notebot
