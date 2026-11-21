#pragma once

#include <filesystem>
#include <vector>

namespace notebot::backup {

class Workspace {
public:
    explicit Workspace(const std::filesystem::path& parent = std::filesystem::temp_directory_path());
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    void preserve() noexcept { preserved_ = true; }
    bool preserved() const noexcept { return preserved_; }
    const std::filesystem::path path;

private:
    bool preserved_ = false;
};

struct FileMapping {
    std::filesystem::path source;
    std::filesystem::path destination;
};

// ZIP names are relative to the archive root. Directories include all descendants.
void writeZip(const std::filesystem::path& archive, const std::vector<FileMapping>& sources);
void extractZip(const std::filesystem::path& archive, const std::filesystem::path& destination);
void copyTree(const std::filesystem::path& source, const std::filesystem::path& destination);
void publishZip(const std::filesystem::path& archive, const std::filesystem::path& destination);

// Keeps original files until commit, and restores them if applying the import fails.
class Restore {
public:
    explicit Restore(const std::vector<FileMapping>& mappings);
    ~Restore();
    Restore(const Restore&) = delete;
    Restore& operator=(const Restore&) = delete;
    void commit() noexcept { committed_ = true; }

private:
    struct Original {
        std::filesystem::path target;
        std::filesystem::path saved;
        bool existed;
        bool touched = false;
    };
    Workspace rollback_;
    std::vector<Original> originals_;
    std::vector<std::filesystem::path> createdDirectories_;
    bool committed_ = false;
    void rollback() noexcept;
};

} // namespace notebot::backup
