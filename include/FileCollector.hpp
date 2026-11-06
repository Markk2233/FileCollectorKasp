#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

class FileCollector {
public:
    FileCollector() = default;
    FileCollector(const FileCollector&) = delete;
    FileCollector& operator=(const FileCollector&) = delete;
    FileCollector(FileCollector&&) = delete;
    FileCollector& operator=(FileCollector&&) = delete;

    enum class Status {
        Ok,
        UnknownFile,
        AlreadyExists,
        OutOfRange,
        NotReady,
    };

    Status CollectFile(uint32_t fileId, size_t fileSize);
    Status OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);
    Status RemoveFile(uint32_t fileId);
    [[nodiscard]] Status ReadRange(uint32_t fileId, size_t pos, std::span<uint8_t> out) const;
    [[nodiscard]] std::shared_ptr<const std::vector<uint8_t>> GetFile(uint32_t fileId) const;


private:
    struct FileState;

    std::shared_ptr<FileState> Find(uint32_t fileId) const;

    mutable std::shared_mutex mapMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<FileState>> files_;
};