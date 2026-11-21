#pragma once

#include "BackupArchive.h"
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace notebot {

class ModDataTransfer {
public:
    enum class Kind { Config, Songs };
    struct Paths {
        std::filesystem::path config;
        std::filesystem::path imgui;
        std::filesystem::path songs;
        std::filesystem::path backups;
    };
    struct Result {
        Kind kind;
        bool importing;
        bool canceled = false;
        std::string error;
        std::filesystem::path archive;
        std::shared_ptr<backup::Workspace> workspace;
    };

    bool busy() const { return task_.valid(); }
    void start(Kind kind, bool importing, const Paths& paths, void* owner, std::wstring title);
    std::optional<Result> poll();

private:
    std::future<Result> task_;
};

} // namespace notebot
