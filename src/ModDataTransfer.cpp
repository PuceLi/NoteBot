#include "ModDataTransfer.h"

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace fs = std::filesystem;

namespace notebot {
namespace {
void checkDialog(HRESULT result) {
    if (FAILED(result))
        throw std::system_error(static_cast<int>(result), std::system_category(), "Windows file dialog");
}

struct Apartment {
    Apartment() { checkDialog(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)); }
    ~Apartment() { CoUninitialize(); }
};

std::optional<fs::path> chooseImport(HWND owner, const std::wstring& title, const fs::path& directory) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    checkDialog(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    DWORD options = 0;
    checkDialog(dialog->GetOptions(&options));
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_STRICTFILETYPES;
    options |= FOS_FILEMUSTEXIST;
    checkDialog(dialog->SetOptions(options));
    const COMDLG_FILTERSPEC filter[]{ {L"ZIP (*.zip)", L"*.zip"} };
    checkDialog(dialog->SetFileTypes(1, filter));
    checkDialog(dialog->SetDefaultExtension(L"zip"));
    checkDialog(dialog->SetTitle(title.c_str()));
    if (fs::is_directory(directory)) {
        ComPtr<IShellItem> folder;
        checkDialog(SHCreateItemFromParsingName(fs::absolute(directory).c_str(), nullptr, IID_PPV_ARGS(&folder)));
        checkDialog(dialog->SetFolder(folder.Get()));
    }
    const HRESULT result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
    checkDialog(result);
    ComPtr<IShellItem> item;
    checkDialog(dialog->GetResult(&item));
    PWSTR name = nullptr;
    checkDialog(item->GetDisplayName(SIGDN_FILESYSPATH, &name));
    const fs::path path(name);
    CoTaskMemFree(name);
    return path;
}

std::wstring defaultName(ModDataTransfer::Kind kind) {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t timestamp[32]{};
    std::wcsftime(timestamp, std::size(timestamp), L"%Y%m%d_%H%M%S", &local);
    return std::wstring(kind == ModDataTransfer::Kind::Config ? L"config_" : L"songs_") + timestamp + L"_backup.zip";
}

void validateContents(ModDataTransfer::Kind kind, const fs::path& root) {
    const bool config = kind == ModDataTransfer::Kind::Config;
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = entry.path().filename();
        if (name != (config ? L"config" : L"songs") && !(config && name == L"data"))
            throw std::runtime_error("This ZIP is not the selected backup type");
    }
    if (config) {
        if (!fs::is_regular_file(root / L"config/config.json"))
            throw std::runtime_error("Backup is missing config/config.json");
        if (fs::exists(root / L"data")) {
            if (!fs::is_directory(root / L"data")) throw std::runtime_error("Invalid backup data directory");
            for (const auto& entry : fs::directory_iterator(root / L"data")) {
                if (entry.path().filename() != L"imgui.ini" || !entry.is_regular_file())
                    throw std::runtime_error("Unexpected file in backup data directory");
            }
        }
    } else if (!fs::is_directory(root / L"songs")) {
        throw std::runtime_error("Backup is missing the songs directory");
    }
}
}

void ModDataTransfer::start(Kind kind, bool importing, const Paths& paths, void* owner, std::wstring title) {
    if (busy()) return;
    auto workspace = std::make_shared<backup::Workspace>();
    if (!importing && kind == Kind::Config) {
        // Snapshot on the UI thread immediately after saving the live config/layout.
        backup::copyTree(paths.config, workspace->path / L"config");
        if (fs::is_regular_file(paths.imgui))
            backup::copyTree(paths.imgui, workspace->path / L"data/imgui.ini");
    }
    task_ = std::async(std::launch::async,
        [kind, importing, paths, owner, title = std::move(title), workspace]() {
            Result result{kind, importing};
            result.workspace = workspace;
            try {
                if (importing) {
                    Apartment apartment;
                    const auto path = chooseImport(static_cast<HWND>(owner), title,
                        paths.backups / (kind == Kind::Config ? L"config" : L"songs"));
                    if (!path) { result.canceled = true; return result; }
                    result.archive = *path;
                    backup::extractZip(*path, workspace->path / L"extracted");
                    validateContents(kind, workspace->path / L"extracted");
                    if (kind == Kind::Songs) {
                        backup::Restore restore({{workspace->path / L"extracted/songs", paths.songs}});
                        restore.commit();
                    }
                } else {
                    std::vector<backup::FileMapping> sources;
                    if (kind == Kind::Config) {
                        sources.push_back({workspace->path / L"config", L"config"});
                        if (fs::exists(workspace->path / L"data"))
                            sources.push_back({workspace->path / L"data", L"data"});
                    } else {
                        sources.push_back({paths.songs, L"songs"});
                    }
                    const auto archive = workspace->path / L"backup.zip";
                    backup::writeZip(archive, sources);
                    const auto directory = paths.backups / (kind == Kind::Config ? L"config" : L"songs");
                    fs::create_directories(directory);
                    const fs::path name(defaultName(kind));
                    auto path = directory / name;
                    for (unsigned suffix = 1; fs::exists(path); ++suffix)
                        path = directory / (name.stem().wstring() + L"_" + std::to_wstring(suffix) + L".zip");
                    backup::publishZip(archive, path);
                    result.archive = path;
                }
            } catch (const std::exception& error) {
                result.error = error.what();
            }
            return result;
        });
}

std::optional<ModDataTransfer::Result> ModDataTransfer::poll() {
    if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return task_.get();
}

} // namespace notebot
