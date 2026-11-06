#include "FileCollector.hpp"
#include "IntervalSet.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

struct FileCollector::FileState {
    explicit FileState(size_t fileSize)
        : size(fileSize)
        , data(std::make_shared<std::vector<uint8_t>>(fileSize))
        , complete(fileSize == 0) {}

    const size_t size;
    const std::shared_ptr<std::vector<uint8_t>> data;

    std::mutex mutex;
    IntervalSet claimed;
    IntervalSet received;
    bool complete;
};

std::shared_ptr<FileCollector::FileState> FileCollector::Find(uint32_t fileId) const {
    std::shared_lock lock(mapMutex_);
    auto it = files_.find(fileId);
    return it != files_.end() ? it->second : nullptr;
}

FileCollector::Status FileCollector::CollectFile(uint32_t fileId, size_t fileSize) {
    auto state = std::make_shared<FileState>(fileSize);

    std::unique_lock lock(mapMutex_);
    return files_.try_emplace(fileId, std::move(state)).second ? Status::Ok
                                                                : Status::AlreadyExists;
}

FileCollector::Status FileCollector::OnNewChunk(uint32_t fileId, size_t pos,
                                                std::vector<uint8_t>&& chunk) {
    auto file = Find(fileId);
    if (!file) {
        return Status::UnknownFile;
    }

    if (pos > file->size || chunk.size() > file->size - pos) {
        return Status::OutOfRange;
    }

    if (chunk.empty()) {
        return Status::Ok;
    }

    const size_t end = pos + chunk.size();

    std::vector<IntervalSet::Range> gaps;
    std::unique_lock lock(file->mutex);
    if (file->complete) {
        return Status::Ok;
    }
    gaps = file->claimed.Missing(pos, end);
    if (gaps.empty()) {
        return Status::Ok;
    }
    file->claimed.Add(pos, end);
    lock.unlock();

    uint8_t* dst = file->data->data();
    for (const auto& [b, e] : gaps) {
        std::copy(chunk.begin() + static_cast<std::ptrdiff_t>(b - pos),
                  chunk.begin() + static_cast<std::ptrdiff_t>(e - pos),
                  dst + b);
    }

    lock.lock();
    for (const auto& [b, e] : gaps) {
        file->received.Add(b, e);
    }
    file->complete = (file->received.Covered() == file->size);

    return Status::Ok;
}

FileCollector::Status FileCollector::RemoveFile(uint32_t fileId) {
    std::shared_ptr<FileState> removed;
    std::lock_guard lock(mapMutex_);
    auto file = files_.find(fileId);
    if (file == files_.end()) {
        return Status::UnknownFile;
    }
    removed = std::move(file->second);
    files_.erase(file);
    return Status::Ok;
}

FileCollector::Status FileCollector::ReadRange(uint32_t fileId, size_t pos,
                                               std::span<uint8_t> out) const {
    auto file = Find(fileId);
    if (!file) {
        return Status::UnknownFile;
    }

    if (pos > file->size || out.size() > file->size - pos) {
        return Status::OutOfRange;
    }

    if (out.empty()) {
        return Status::Ok;
    }

    std::unique_lock lock(file->mutex);
    if (!file->received.Covers(pos, pos + out.size())) {
        return Status::NotReady;
    }
    lock.unlock();

    std::copy_n(file->data->data() + pos, out.size(), out.begin());
    return Status::Ok;
}

std::shared_ptr<const std::vector<uint8_t>> FileCollector::GetFile(uint32_t fileId) const {
    auto file = Find(fileId);
    if (!file) {
        return nullptr;
    }

    std::lock_guard lock(file->mutex);
    return file->complete ? file->data : nullptr;
}