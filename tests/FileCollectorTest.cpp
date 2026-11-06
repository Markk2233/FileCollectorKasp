#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#include "FileCollector.hpp"

using Status = FileCollector::Status;

std::vector<uint8_t> Bytes(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> MakeData(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(i * 31 + 7);
    }
    return data;
}

struct Chunk {
    size_t pos;
    std::vector<uint8_t> bytes;
};

std::vector<Chunk> MakeChunks(const std::vector<uint8_t>& data, std::mt19937& rng) {
    std::vector<Chunk> chunks;
    std::uniform_int_distribution<size_t> lenDist(1, 512);

    for (size_t pos = 0; pos < data.size();) {
        const size_t n = std::min(lenDist(rng), data.size() - pos);
        chunks.push_back({pos, {data.begin() + pos, data.begin() + pos + n}});
        pos += n;
    }

    std::uniform_int_distribution<size_t> posDist(0, data.size() - 1);
    const size_t extra = chunks.size();
    for (size_t i = 0; i < extra; ++i) {
        const size_t p = posDist(rng);
        const size_t n = std::min(lenDist(rng), data.size() - p);
        chunks.push_back({p, {data.begin() + p, data.begin() + p + n}});
    }

    std::shuffle(chunks.begin(), chunks.end(), rng);
    return chunks;
}

void SendFromThreads(FileCollector& collector, uint32_t fileId,
                     std::vector<Chunk>& chunks, size_t threadCount) {
    std::vector<std::thread> threads;
    for (size_t t = 0; t < threadCount; ++t) {
        threads.emplace_back([&, t] {
            for (size_t i = t; i < chunks.size(); i += threadCount) {
                EXPECT_EQ(collector.OnNewChunk(fileId, chunks[i].pos, std::move(chunks[i].bytes)),
                          Status::Ok);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
}

TEST(FileCollectorTest, ExampleFromTask) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    EXPECT_EQ(c.OnNewChunk(1, 0, Bytes("ABCD")), Status::Ok);
    EXPECT_EQ(c.OnNewChunk(1, 2, Bytes("CDEF")), Status::Ok);
    EXPECT_EQ(c.OnNewChunk(1, 6, Bytes("GHIJ")), Status::Ok);

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("ABCDEFGHIJ"));
}

TEST(FileCollectorTest, OutOfOrderChunks) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    c.OnNewChunk(1, 6, Bytes("GHIJ"));
    c.OnNewChunk(1, 0, Bytes("AB"));
    c.OnNewChunk(1, 2, Bytes("CDEF"));

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("ABCDEFGHIJ"));
}

TEST(FileCollectorTest, NotCompleteUntilAllReceived) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    c.OnNewChunk(1, 0, Bytes("ABCD"));
    EXPECT_EQ(c.GetFile(1), nullptr);
    c.OnNewChunk(1, 6, Bytes("GHIJ"));
    EXPECT_EQ(c.GetFile(1), nullptr);
    c.OnNewChunk(1, 4, Bytes("EF"));
    EXPECT_NE(c.GetFile(1), nullptr);
}

TEST(FileCollectorTest, DuplicateChunks) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 4), Status::Ok);

    EXPECT_EQ(c.OnNewChunk(1, 0, Bytes("AB")), Status::Ok);
    EXPECT_EQ(c.OnNewChunk(1, 0, Bytes("AB")), Status::Ok);
    EXPECT_EQ(c.GetFile(1), nullptr);

    c.OnNewChunk(1, 2, Bytes("CD"));
    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("ABCD"));
}

TEST(FileCollectorTest, ChunkInsideExisting) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    c.OnNewChunk(1, 0, Bytes("ABCDEF"));
    c.OnNewChunk(1, 2, Bytes("CD"));
    EXPECT_EQ(c.GetFile(1), nullptr);

    c.OnNewChunk(1, 6, Bytes("GHIJ"));
    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("ABCDEFGHIJ"));
}

TEST(FileCollectorTest, ChunkSpanningSeveralReceived) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    c.OnNewChunk(1, 0, Bytes("A"));
    c.OnNewChunk(1, 4, Bytes("E"));
    c.OnNewChunk(1, 9, Bytes("J"));
    c.OnNewChunk(1, 0, Bytes("ABCDEFGHIJ"));

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("ABCDEFGHIJ"));
}

TEST(FileCollectorTest, SingleChunkWholeFile) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 3), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("XYZ"));

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, Bytes("XYZ"));
}

TEST(FileCollectorTest, ZeroSizeFileIsCompleteImmediately) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 0), Status::Ok);

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_TRUE(file->empty());
}

TEST(FileCollectorTest, MultipleFilesAreIndependent) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 2), Status::Ok);
    ASSERT_EQ(c.CollectFile(2, 2), Status::Ok);

    c.OnNewChunk(1, 0, Bytes("AB"));
    EXPECT_NE(c.GetFile(1), nullptr);
    EXPECT_EQ(c.GetFile(2), nullptr);

    c.OnNewChunk(2, 0, Bytes("CD"));
    ASSERT_NE(c.GetFile(1), nullptr);
    ASSERT_NE(c.GetFile(2), nullptr);
    EXPECT_EQ(*c.GetFile(1), Bytes("AB"));
    EXPECT_EQ(*c.GetFile(2), Bytes("CD"));
}

TEST(FileCollectorTest, GetFileDoesNotCopy) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 2), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("AB"));

    auto a = c.GetFile(1);
    auto b = c.GetFile(1);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a.get(), b.get());
}

TEST(FileCollectorTest, LateChunkDoesNotModifyCompletedFile) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 4), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("ABCD"));
    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(c.OnNewChunk(1, 0, Bytes("ZZZZ")), Status::Ok);
    EXPECT_EQ(*file, Bytes("ABCD"));
}

TEST(FileCollectorTest, ReadRangeOnPartialFile) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("ABCD"));
    c.OnNewChunk(1, 6, Bytes("GHIJ"));

    std::vector<uint8_t> buf(4);
    EXPECT_EQ(c.ReadRange(1, 0, buf), Status::Ok);
    EXPECT_EQ(buf, Bytes("ABCD"));

    EXPECT_EQ(c.ReadRange(1, 6, buf), Status::Ok);
    EXPECT_EQ(buf, Bytes("GHIJ"));

    EXPECT_EQ(c.ReadRange(1, 2, buf), Status::NotReady);
}

TEST(FileCollectorTest, ReadRangeSubspan) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 4), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("ABCD"));

    std::vector<uint8_t> buf(10, 0);
    EXPECT_EQ(c.ReadRange(1, 1, std::span(buf).first(2)), Status::Ok);
    EXPECT_EQ(buf[0], 'B');
    EXPECT_EQ(buf[1], 'C');
    EXPECT_EQ(buf[2], 0);
}

TEST(FileCollectorTest, ReadRangeEmptyAndOutOfRange) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    EXPECT_EQ(c.ReadRange(1, 5, std::span<uint8_t>{}), Status::Ok);

    std::vector<uint8_t> buf(4);
    EXPECT_EQ(c.ReadRange(1, 8, buf), Status::OutOfRange);
    EXPECT_EQ(c.ReadRange(1, 11, std::span<uint8_t>{}), Status::OutOfRange);
}

TEST(FileCollectorTest, CollectFileTwice) {
    FileCollector c;
    EXPECT_EQ(c.CollectFile(1, 10), Status::Ok);
    EXPECT_EQ(c.CollectFile(1, 10), Status::AlreadyExists);
    EXPECT_EQ(c.CollectFile(1, 20), Status::AlreadyExists);
}

TEST(FileCollectorTest, UnknownFile) {
    FileCollector c;
    std::vector<uint8_t> buf(1);
    EXPECT_EQ(c.OnNewChunk(42, 0, Bytes("A")), Status::UnknownFile);
    EXPECT_EQ(c.ReadRange(42, 0, buf), Status::UnknownFile);
    EXPECT_EQ(c.GetFile(42), nullptr);
}

TEST(FileCollectorTest, ChunkOutOfRange) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    EXPECT_EQ(c.OnNewChunk(1, 8, Bytes("ABC")), Status::OutOfRange);
    EXPECT_EQ(c.OnNewChunk(1, 11, Bytes("A")), Status::OutOfRange);
    EXPECT_EQ(c.OnNewChunk(1, std::numeric_limits<size_t>::max(), Bytes("AB")),
              Status::OutOfRange);
    EXPECT_EQ(c.OnNewChunk(1, 7, Bytes("ABC")), Status::Ok);
}

TEST(FileCollectorTest, EmptyChunk) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 10), Status::Ok);

    EXPECT_EQ(c.OnNewChunk(1, 3, {}), Status::Ok);
    EXPECT_EQ(c.OnNewChunk(1, 10, {}), Status::Ok);
    EXPECT_EQ(c.GetFile(1), nullptr);
}

TEST(FileCollectorTest, RemoveFile) {
    FileCollector c;
    ASSERT_EQ(c.CollectFile(1, 2), Status::Ok);
    c.OnNewChunk(1, 0, Bytes("AB"));
    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(c.RemoveFile(1), Status::Ok);
    EXPECT_EQ(c.RemoveFile(1), Status::UnknownFile);
    EXPECT_EQ(c.GetFile(1), nullptr);
    EXPECT_EQ(c.OnNewChunk(1, 0, Bytes("AB")), Status::UnknownFile);
    EXPECT_EQ(*file, Bytes("AB"));

    EXPECT_EQ(c.CollectFile(1, 3), Status::Ok);
}

TEST(FileCollectorConcurrencyTest, RemoveWhileWriting) {
    constexpr size_t kSize = 1 << 16;
    FileCollector c;
    const auto data = MakeData(kSize);
    ASSERT_EQ(c.CollectFile(1, kSize), Status::Ok);

    std::mt19937 rng(5);
    auto chunks = MakeChunks(data, rng);

    std::vector<std::thread> writers;
    for (size_t t = 0; t < 4; ++t) {
        writers.emplace_back([&, t] {
            for (size_t i = t; i < chunks.size(); i += 4) {
                auto st = c.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].bytes));
                EXPECT_TRUE(st == Status::Ok || st == Status::UnknownFile);
            }
        });
    }
    std::thread remover([&] { EXPECT_EQ(c.RemoveFile(1), Status::Ok); });

    for (auto& th : writers) {
        th.join();
    }
    remover.join();
    EXPECT_EQ(c.GetFile(1), nullptr);
}

TEST(FileCollectorConcurrencyTest, ManyThreadsOneFile) {
    constexpr size_t kSize = 1 << 16;
    constexpr size_t kThreads = 8;

    FileCollector c;
    const auto data = MakeData(kSize);
    ASSERT_EQ(c.CollectFile(1, kSize), Status::Ok);

    std::mt19937 rng(1);
    auto chunks = MakeChunks(data, rng);
    SendFromThreads(c, 1, chunks, kThreads);

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, data);
}

TEST(FileCollectorConcurrencyTest, ManyFilesInParallel) {
    constexpr size_t kSize = 1 << 14;
    constexpr uint32_t kFiles = 16;

    FileCollector c;
    std::vector<std::vector<uint8_t>> expected;
    for (uint32_t id = 0; id < kFiles; ++id) {
        auto d = MakeData(kSize);
        std::rotate(d.begin(), d.begin() + id, d.end());
        expected.push_back(std::move(d));
    }

    std::vector<std::thread> threads;
    for (uint32_t id = 0; id < kFiles; ++id) {
        threads.emplace_back([&, id] {
            ASSERT_EQ(c.CollectFile(id, kSize), Status::Ok);
            std::mt19937 rng(id);
            for (auto& ch : MakeChunks(expected[id], rng)) {
                EXPECT_EQ(c.OnNewChunk(id, ch.pos, std::move(ch.bytes)), Status::Ok);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    for (uint32_t id = 0; id < kFiles; ++id) {
        auto file = c.GetFile(id);
        ASSERT_NE(file, nullptr) << "file " << id;
        EXPECT_EQ(*file, expected[id]) << "file " << id;
    }
}

TEST(FileCollectorConcurrencyTest, ReadsWhileWriting) {
    constexpr size_t kSize = 1 << 16;
    constexpr size_t kReadLen = 64;

    FileCollector c;
    const auto data = MakeData(kSize);
    ASSERT_EQ(c.CollectFile(1, kSize), Status::Ok);

    std::mt19937 rng(7);
    auto chunks = MakeChunks(data, rng);

    std::atomic<bool> done{false};
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&, r] {
            std::mt19937 local(100 + r);
            std::uniform_int_distribution<size_t> posDist(0, kSize - kReadLen);
            std::vector<uint8_t> buf(kReadLen);
            while (!done.load()) {
                const size_t p = posDist(local);
                if (c.ReadRange(1, p, buf) == Status::Ok) {
                    ASSERT_TRUE(std::equal(buf.begin(), buf.end(), data.begin() + p))
                        << "pos " << p;
                }
                if (auto f = c.GetFile(1)) {
                    ASSERT_TRUE(*f == data);
                }
            }
        });
    }

    SendFromThreads(c, 1, chunks, 4);
    done = true;
    for (auto& th : readers) {
        th.join();
    }

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, data);
}

TEST(FileCollectorConcurrencyTest, ConcurrentCollectSameId) {
    constexpr int kThreads = 16;

    FileCollector c;
    std::atomic<int> okCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            if (c.CollectFile(1, 100) == Status::Ok) {
                ++okCount;
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(okCount.load(), 1);
}

TEST(FileCollectorConcurrencyTest, AllThreadsSendAllChunks) {
    constexpr size_t kSize = 1 << 16;
    constexpr size_t kThreads = 8;

    FileCollector c;
    const auto data = MakeData(kSize);
    ASSERT_EQ(c.CollectFile(1, kSize), Status::Ok);

    std::mt19937 rng(3);
    const auto base = MakeChunks(data, rng);

    std::vector<std::thread> threads;
    for (size_t t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            auto chunks = base;
            std::mt19937 local(static_cast<unsigned>(t));
            std::shuffle(chunks.begin(), chunks.end(), local);
            for (auto& ch : chunks) {
                EXPECT_EQ(c.OnNewChunk(1, ch.pos, std::move(ch.bytes)), Status::Ok);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    auto file = c.GetFile(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(*file, data);
}