#include "BackupArchive.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <windows.h>
#include <objbase.h>
#include <minizip/mz.h>
#include <minizip/mz_strm.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_zip_rw.h>

namespace fs = std::filesystem;

namespace notebot::backup {
namespace {
std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}

void checkZip(int32_t result, const char* operation) {
    if (result != MZ_OK)
        throw std::runtime_error(std::string(operation) + " (ZIP " + std::to_string(result) + ")");
}

void rejectLink(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Links and reparse points are not supported: " + utf8(path));
}

void checkTarget(const fs::path& path) {
    for (auto part = fs::absolute(path).lexically_normal(); !part.empty();) {
        rejectLink(part);
        const auto parent = part.parent_path();
        if (parent == part) break;
        part = parent;
    }
}

fs::path makeWorkspace(const fs::path& parent) {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create backup workspace ID");
    wchar_t name[40]{};
    StringFromGUID2(id, name, 40);
    const auto path = fs::absolute(parent) / (std::wstring(L"notebot-") + name);
    if (!fs::create_directory(path)) throw std::runtime_error("Cannot create backup workspace");
    return path;
}

template <typename F>
void visitTree(const fs::path& source, const fs::path& name, F&& visit) {
    rejectLink(source);
    visit(source, name);
    if (fs::is_directory(source)) {
        for (const auto& entry : fs::recursive_directory_iterator(source)) {
            rejectLink(entry.path());
            visit(entry.path(), name / entry.path().lexically_relative(source));
        }
    }
}

struct Reader {
    void* handle = mz_zip_reader_create();
    Reader() { if (!handle) throw std::bad_alloc(); }
    ~Reader() { mz_zip_reader_delete(&handle); }
};

fs::path entryPath(std::string name) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::runtime_error("Empty or invalid ZIP entry name");
    std::replace(name.begin(), name.end(), '\\', '/');
    while (name.starts_with("./")) name.erase(0, 2);
    if (name.empty()) throw std::runtime_error("Empty ZIP entry path");
    if (name.front() == '/') throw std::runtime_error("Absolute ZIP entry path");
    fs::path result;
    size_t offset = 0;
    while (offset < name.size()) {
        const auto end = name.find('/', offset);
        const auto component = name.substr(offset, end == std::string::npos ? end : end - offset);
        if (component.empty() || component == "." || component == ".." ||
            component.back() == '.' || component.back() == ' ' ||
            component.find_first_of(":<>\"|?*") != std::string::npos ||
            std::any_of(component.begin(), component.end(), [](unsigned char c) { return c < 32; }))
            throw std::runtime_error("Unsafe ZIP entry path");
        auto base = component.substr(0, component.find('.'));
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
            (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) &&
             base[3] >= '1' && base[3] <= '9'))
            throw std::runtime_error("Reserved Windows filename in ZIP");
        result /= fs::path(std::u8string(component.begin(), component.end()));
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (result.has_root_path()) throw std::runtime_error("Rooted ZIP entry path");
    return result;
}

struct PathLess {
    bool operator()(const fs::path& a, const fs::path& b) const {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};

fs::path systemTar() {
    wchar_t directory[32768]{};
    const auto length = GetSystemDirectoryW(directory, static_cast<UINT>(std::size(directory)));
    if (!length || length >= std::size(directory))
        throw std::runtime_error("Cannot locate Windows tar.exe");
    const auto exe = fs::path(directory) / L"tar.exe";
    if (!fs::is_regular_file(exe))
        throw std::runtime_error("Windows tar.exe is unavailable on this system");
    return exe;
}

std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

void runSystemZip(bool extracting, const fs::path& source, const fs::path& destination,
                  const fs::path& workingDirectory) {
    const auto exe = systemTar();
    const std::vector<std::wstring> arguments = extracting
        ? std::vector<std::wstring>{L"-xf", fs::absolute(source).wstring(), L"-C", fs::absolute(destination).wstring()}
        : std::vector<std::wstring>{L"-c", L"--format=zip", L"--options", L"zip:hdrcharset=UTF-8",
                                   L"-f", fs::absolute(destination).wstring(), L"-C", fs::absolute(source).wstring(), L"."};
    auto command = quoteArgument(exe.wstring());
    for (const auto& argument : arguments) command += L' ' + quoteArgument(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, workingDirectory.c_str(), &startup, &process))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot launch Windows ZIP operation");
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool completed = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (!completed || exitCode != 0)
        throw std::runtime_error("Windows tar ZIP operation failed (exit " + std::to_string(exitCode) + ")");
}
}

Workspace::Workspace(const fs::path& parent) : path(makeWorkspace(parent)) {}
Workspace::~Workspace() {
    if (preserved_) return;
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

void copyTree(const fs::path& source, const fs::path& destination) {
    visitTree(source, destination, [](const fs::path& file, const fs::path& target) {
        if (fs::is_directory(file)) fs::create_directories(target);
        else if (fs::is_regular_file(file)) {
            fs::create_directories(target.parent_path());
            fs::copy_file(file, target, fs::copy_options::overwrite_existing);
        } else throw std::runtime_error("Unsupported backup file: " + utf8(file));
    });
}

void writeZip(const fs::path& archive, const std::vector<FileMapping>& sources) {
    Workspace staging;
    const auto input = staging.path / L"input";
    fs::create_directory(input);
    for (const auto& mapping : sources) {
        const auto name = entryPath(utf8(mapping.destination));
        copyTree(mapping.source, input / name);
    }
    if (sources.empty()) throw std::runtime_error("No backup sources");
    runSystemZip(false, input, staging.path / L"backup.zip", input);
    publishZip(staging.path / L"backup.zip", archive);
}

void extractZip(const fs::path& archive, const fs::path& destination) {
    Reader zip;
    checkZip(mz_zip_reader_open_file(zip.handle, utf8(archive).c_str()), "Cannot open ZIP");
    fs::create_directories(destination);
    std::map<fs::path, int64_t, PathLess> names;
    uint64_t totalSize = 0;
    int32_t result = mz_zip_reader_goto_first_entry(zip.handle);
    while (result == MZ_OK) {
        mz_zip_file* info = nullptr;
        checkZip(mz_zip_reader_entry_get_info(zip.handle, &info), "Cannot read ZIP entry");
        if (!info || !info->filename || info->uncompressed_size < 0)
            throw std::runtime_error("Invalid ZIP entry metadata");
        const auto unixType = (info->external_fa >> 16) & 0170000;
        if ((info->linkname && *info->linkname) || unixType == 0120000 ||
            (info->external_fa & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("ZIP links are not supported");
        if (info->flag & MZ_ZIP_FLAG_ENCRYPTED)
            throw std::runtime_error("Password-protected ZIP backups are not supported");
        const bool directory = mz_zip_reader_entry_is_dir(zip.handle) == MZ_OK;
        const std::string name(info->filename, info->filename_size);
        if (directory && (name == "./" || name == ".")) {
            result = mz_zip_reader_goto_next_entry(zip.handle);
            continue;
        }
        const auto relative = entryPath(name);
        if (!names.emplace(relative, directory ? -1 : info->uncompressed_size).second)
            throw std::runtime_error("Duplicate ZIP entry path");
        // Bound extraction before allocating or writing data from untrusted metadata.
        constexpr uint64_t maxSize = 16ULL * 1024 * 1024 * 1024;
        if (names.size() > 100000 || static_cast<uint64_t>(info->uncompressed_size) > maxSize - totalSize)
            throw std::runtime_error("ZIP exceeds backup extraction limits");
        totalSize += static_cast<uint64_t>(info->uncompressed_size);
        checkTarget(destination / relative);
        if (!directory) {
            checkZip(mz_zip_reader_entry_open(zip.handle), "Cannot validate ZIP entry");
            std::array<char, 64 * 1024> buffer{};
            int32_t count = 0;
            uint64_t readSize = 0;
            while ((count = mz_zip_reader_entry_read(zip.handle, buffer.data(), static_cast<int32_t>(buffer.size()))) > 0) {
                readSize += static_cast<uint64_t>(count);
                if (readSize > static_cast<uint64_t>(info->uncompressed_size))
                    throw std::runtime_error("ZIP entry exceeds its declared size");
            }
            if (count < 0) checkZip(count, "Cannot validate ZIP data");
            checkZip(mz_zip_reader_entry_close(zip.handle), "ZIP CRC validation failed");
            if (readSize != static_cast<uint64_t>(info->uncompressed_size))
                throw std::runtime_error("ZIP entry size is incorrect");
        }
        result = mz_zip_reader_goto_next_entry(zip.handle);
    }
    if (result != MZ_END_OF_LIST) checkZip(result, "Cannot enumerate ZIP");
    if (names.empty()) throw std::runtime_error("Backup ZIP is empty");
    checkZip(mz_zip_reader_close(zip.handle), "Cannot close ZIP");
    runSystemZip(true, archive, destination, fs::absolute(destination));
    for (const auto& entry : fs::recursive_directory_iterator(destination)) {
        rejectLink(entry.path());
        if (!entry.is_directory() && !names.contains(entry.path().lexically_relative(destination)))
            throw std::runtime_error("Unexpected extracted ZIP entry");
    }
    for (const auto& [relative, size] : names) {
        const auto target = destination / relative;
        if (size < 0 ? !fs::is_directory(target) : !fs::is_regular_file(target) ||
            fs::file_size(target) != static_cast<uint64_t>(size))
            throw std::runtime_error("Extracted ZIP entry is missing or has an incorrect size");
    }
}

void publishZip(const fs::path& archive, const fs::path& destination) {
    checkTarget(destination);
    Workspace temp(destination.parent_path());
    const auto staged = temp.path / L"backup.zip";
    fs::copy_file(archive, staged);
    if (!MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot save ZIP");
}

Restore::Restore(const std::vector<FileMapping>& mappings) {
    try {
        std::vector<FileMapping> files;
        std::set<fs::path, PathLess> targets;
        std::map<fs::path, size_t, PathLess> originalIndices;
        for (const auto& mapping : mappings) {
            visitTree(mapping.source, mapping.destination, [&](const fs::path& source, const fs::path& target) {
                checkTarget(target);
                if (!targets.insert(fs::absolute(target).lexically_normal()).second)
                    throw std::runtime_error("Conflicting import destinations");
                if (fs::is_directory(source)) {
                    if (fs::exists(target) && !fs::is_directory(target))
                        throw std::runtime_error("Import directory conflicts with existing file");
                } else if (fs::is_regular_file(source)) {
                    if (fs::exists(target) && !fs::is_regular_file(target))
                        throw std::runtime_error("Import file conflicts with existing directory");
                    files.push_back({source, target});
                } else throw std::runtime_error("Unsupported import file");
            });
        }
        for (const auto& file : files) {
            const bool exists = fs::exists(file.destination);
            const auto saved = rollback_.path / std::to_string(originals_.size());
            if (exists) fs::copy_file(file.destination, saved);
            originalIndices.emplace(file.destination, originals_.size());
            originals_.push_back({file.destination, saved, exists});
        }
        for (const auto& mapping : mappings) {
            visitTree(mapping.source, mapping.destination, [&](const fs::path& source, const fs::path& target) {
                const auto directory = fs::is_directory(source) ? target : target.parent_path();
                std::vector<fs::path> missing;
                for (auto part = directory; !part.empty() && !fs::exists(part); part = part.parent_path())
                    missing.push_back(part);
                for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
                    fs::create_directory(*it);
                    createdDirectories_.push_back(*it);
                }
                if (fs::is_regular_file(source)) {
                    originals_[originalIndices.at(target)].touched = true;
                    fs::copy_file(source, target, fs::copy_options::overwrite_existing);
                }
            });
        }
    } catch (...) {
        rollback();
        if (rollback_.preserved())
            throw std::runtime_error("Import failed; original files were kept for recovery: " + utf8(rollback_.path));
        throw;
    }
}

void Restore::rollback() noexcept {
    for (auto it = originals_.rbegin(); it != originals_.rend(); ++it) {
        if (!it->touched) continue;
        std::error_code ignored;
        if (it->existed) fs::copy_file(it->saved, it->target, fs::copy_options::overwrite_existing, ignored);
        else fs::remove(it->target, ignored);
        if (ignored) rollback_.preserve();
    }
    for (auto it = createdDirectories_.rbegin(); it != createdDirectories_.rend(); ++it) {
        std::error_code ignored;
        fs::remove(*it, ignored);
    }
}

Restore::~Restore() { if (!committed_) rollback(); }

} // namespace notebot::backup
