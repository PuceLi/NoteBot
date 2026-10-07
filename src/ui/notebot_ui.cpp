#include "notebot_ui.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <random>
#include <cfloat>
#include <vector>

namespace meteor {
namespace {

constexpr ImU32 color(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
constexpr float kPad = 6.0f;
constexpr float kSpacing = 3.0f;

std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool meteorButton(const char* label, float width = 0.0f) {
    const ImVec2 text = ImGui::CalcTextSize(label);
    if (width <= 0.0f) width = text.x + kPad * 2.0f;
    const ImVec2 size(width, text.y + kPad * 2.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 outline = held ? color(20, 20, 20) : hovered ? color(10, 10, 10) : color(0, 0, 0);
    const ImU32 fill = held ? color(40, 40, 40, 200) : hovered ? color(30, 30, 30, 200) : color(20, 20, 20, 200);
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), outline);
    draw->AddRectFilled(ImVec2(pos.x + 2, pos.y + 2), ImVec2(pos.x + size.x - 2, pos.y + size.y - 2), fill);
    draw->AddText(ImVec2(pos.x + (size.x - text.x) / 2, pos.y + kPad), color(255, 255, 255), label);
    return ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
}

void separator(float width) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(p.x,p.y+1),ImVec2(p.x+width,p.y+1),color(255,255,255,170),1.0f);
    ImGui::Dummy(ImVec2(width, 3));
}

bool containsIgnoreCase(std::string haystack, std::string needle) {
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (needle.empty()) return true;
    bool wordsFound = true;
    size_t start = 0;
    while (start < needle.size()) {
        const size_t end = needle.find(' ', start);
        const std::string word = needle.substr(start, end == std::string::npos ? end : end - start);
        if (!word.empty() && haystack.find(word) == std::string::npos) { wordsFound = false; break; }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (wordsFound) return true;
    std::vector<int> previous(haystack.size() + 1), current(haystack.size() + 1);
    for (size_t j = 0; j <= haystack.size(); ++j) previous[j] = int(j);
    for (size_t i = 1; i <= needle.size(); ++i) {
        current[0] = int(i * 8);
        for (size_t j = 1; j <= haystack.size(); ++j) {
            current[j] = std::min({previous[j] + 8, current[j - 1] + 1,
                previous[j - 1] + (needle[i - 1] == haystack[j - 1] ? 0 : 8)});
        }
        previous.swap(current);
    }
    return previous[haystack.size()] < int(haystack.size() / 2);
}

void header(const char* title, bool& expanded) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFontSize() * 1.25f + 8.0f;
    const ImVec2 end(start.x + width, start.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, color(145, 61, 226));
    const ImVec2 text = ImGui::CalcTextSize(title);
    draw->AddText(ImVec2(start.x + (width - text.x) * 0.5f, start.y + (height - text.y) * 0.5f), color(255, 255, 255), title);
    const float tx = end.x - 13.0f, ty = start.y + height * 0.5f;
    if (expanded) draw->AddTriangleFilled(ImVec2(tx - 5, ty - 2), ImVec2(tx + 5, ty - 2), ImVec2(tx, ty + 4), color(20, 20, 20, 200));
    else draw->AddTriangleFilled(ImVec2(tx - 2, ty - 5), ImVec2(tx - 2, ty + 5), ImVec2(tx + 4, ty), color(20, 20, 20, 200));
    ImGui::InvisibleButton("##header", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    static bool wasDragged = false;
    if (ImGui::IsItemActivated()) wasDragged = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) expanded = !expanded;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        wasDragged = true;
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(p.x + d.x, p.y + d.y));
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !wasDragged) expanded = !expanded;
}

} // namespace

NotebotUi::NotebotUi(std::filesystem::path songsDirectory) : songsDirectory_(std::move(songsDirectory)) {
    refreshSongs();
}

void NotebotUi::setSongsDirectory(std::filesystem::path directory) {
    songsDirectory_ = std::move(directory);
    refreshSongs();
}

void NotebotUi::refreshSongs() {
    songFiles_.clear();
    std::error_code error;
    songFolderAvailable_ = std::filesystem::is_directory(songsDirectory_, error);
    if (!songFolderAvailable_) return;
    for (std::filesystem::directory_iterator it(songsDirectory_,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end && !error; it.increment(error)) {
        std::error_code fileError;
        if (!it->is_regular_file(fileError)) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == ".nbs") songFiles_.push_back(it->path());
    }
    std::sort(songFiles_.begin(), songFiles_.end(), [](const auto& left, const auto& right) {
        return utf8(left.filename()) < utf8(right.filename());
    });
}

std::string NotebotUi::getStatus() const {
    if (!statusOverride.empty()) return statusOverride;
    if (!active) return "Module disabled.";
    if (!songLoaded) return "No song loaded.";
    if (playing) return "Playing song. " + std::to_string(currentTick) + "/" + std::to_string(lastTick);
    if (stage == NotebotStage::Playing) return "Ready to play.";
    if (stage == NotebotStage::SetUp || stage == NotebotStage::Tune || stage == NotebotStage::WaitingToCheckNoteblocks)
        return "Setting up the noteblocks.";
    switch (stage) {
    case NotebotStage::LoadingSong: return "Stage: LoadingSong.";
    case NotebotStage::None: return "Stage: None.";
    default: return "Stage: Playing.";
    }
}

std::string NotebotUi::getInfoString() const {
    if (stage == NotebotStage::None) return "None";
    const char* mode = playingMode == NotebotPlayingMode::Preview ? "Preview" :
        playingMode == NotebotPlayingMode::Noteblocks ? "Noteblocks" : "None";
    const char* stageName = stage == NotebotStage::LoadingSong ? "LoadingSong" :
        stage == NotebotStage::SetUp ? "SetUp" : stage == NotebotStage::Tune ? "Tune" :
        stage == NotebotStage::WaitingToCheckNoteblocks ? "WaitingToCheckNoteblocks" : "Playing";
    return std::string(mode) + " | " + stageName;
}

void NotebotUi::draw() {
    if (editingColor_ >= 0) { drawColorEditor(); return; }
    if (showModule) drawModule();
    if (showSongs) drawSongs();
}

void NotebotUi::drawSongs() {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { showSongs = false; showModule = true; return; }
    if (focusSearch_) refreshSongs();
    size_t songCount = 0;
    for (const auto& path : songFiles_)
        if (containsIgnoreCase(utf8(path.filename()), filter_)) ++songCount;
    const float fontSize = ImGui::GetFontSize();
    const float headerHeight = fontSize * 1.25f + 8.0f;
    const float rowHeight = fontSize + kPad * 2 + kSpacing * 3;
    const float wantedListHeight = songCount == 0 ? fontSize * 3.0f : rowHeight * songCount;
    const float listHeight = std::max(28.0f, std::min(wantedListHeight, std::max(100.0f, ImGui::GetMainViewport()->Size.y - 238.0f)));
    const float windowHeight = songsExpanded_ ? headerHeight + 8 + (fontSize + kPad * 2) * 2 + kSpacing * 2 + listHeight + 11 : headerHeight;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(416, windowHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 150), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kSpacing, kSpacing));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("Notebot Songs##meteor", &showSongs, flags)) {
        header("Notebot Songs", songsExpanded_);
        if (songsExpanded_) {
            ImGui::SetCursorPosX(8);
            ImGui::BeginGroup();
            const float contentWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - 8.0f);
            if (meteorButton("Random Song", (contentWidth - 3) / 2)) {
                if (!songFiles_.empty() && actions.loadSong) {
                    static std::mt19937 rng(std::random_device{}());
                    std::uniform_int_distribution<size_t> distribution(0, songFiles_.size() - 1);
                    actions.loadSong(songFiles_[distribution(rng)]);
                    showSongs = false;
                    showModule = true;
                }
            }
            ImGui::SameLine(0, 3);
            if (meteorButton("Refresh", (contentWidth - 3) / 2)) refreshSongs();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(20/255.f, 20/255.f, 20/255.f, 160/255.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 1));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1, 1, 1, 20/255.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPad, kPad));
            ImGui::SetNextItemWidth(contentWidth);
            if (focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
            if (ImGui::InputTextWithHint("##song-search", "Search for the songs...", searchBuffer_, sizeof(searchBuffer_))) filter_ = searchBuffer_;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { searchBuffer_[0] = '\0'; filter_.clear(); }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            std::vector<std::filesystem::path> visibleSongs;
            const bool exists = songFolderAvailable_;
            if (exists) {
                for (const auto& path : songFiles_) {
                    const std::string filename = utf8(path.filename());
                    if (!containsIgnoreCase(filename, filter_)) continue;
                    visibleSongs.push_back(path);
                }
            }
            const float listHeight = std::max(28.0f, ImGui::GetContentRegionAvail().y - 8.0f);
            ImGui::BeginChild("##songs", ImVec2(contentWidth, listHeight), false);
            for (const auto& path : visibleSongs) {
                    separator(contentWidth);
                    const std::string filename = utf8(path.filename());
                    ImGui::PushID(filename.c_str());
                    const std::string name = utf8(path.stem());
                    ImGui::TextUnformatted(name.c_str());
                    const float loadWidth = ImGui::CalcTextSize("Load").x + 12;
                    ImGui::SameLine(contentWidth - loadWidth);
                    if (meteorButton("Load") && actions.loadSong) {
                        actions.loadSong(path);
                        showSongs = false;
                        showModule = true;
                    }
                    ImGui::PopID();
            }
            if (!exists) ImGui::TextUnformatted("Song folder is missing.");
            if (visibleSongs.empty()) {
                const char* empty = "No songs found.";
                ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize(empty).x) / 2.0f);
                ImGui::TextUnformatted(empty);
            }
            ImGui::EndChild();
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 8));
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace
