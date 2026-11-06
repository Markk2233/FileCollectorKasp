# FileCollector

A thread-safe class for assembling files from chunks that arrive in arbitrary order, may overlap, and may be duplicated. Several threads can assemble the same file concurrently.

## Requirements

- CMake ≥ 3.20
- A C++20 compiler: GCC 10+, Clang 12+, MSVC 2019 16.10+
- Internet access during the first configuration: GoogleTest is downloaded via `FetchContent`

## Building and running tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Sanitizers

```bash
# ThreadSanitizer (Linux / macOS; on Windows, use WSL)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DFC_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DFC_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

| Option | Default | Description |
|---|---|---|
| `FC_BUILD_TESTS` | `ON` | Build the tests |
| `FC_ENABLE_TSAN` | `OFF` | ThreadSanitizer |
| `FC_ENABLE_ASAN` | `OFF` | AddressSanitizer + UBSan |

Before enabling a sanitizer, CMake checks that the toolchain can compile and link with it. If it cannot (for example, MinGW), a warning is printed and the build proceeds without the sanitizer.

## Architecture

### Data storage

The file size is known in advance, so `CollectFile` immediately allocates a contiguous buffer for the whole file. Each chunk is copied directly into place at offset `pos`. There is no need to stitch the file together at the end, and it always lies contiguously in memory.

`IntervalSet` keeps track of what has been received: it is a `std::map<begin, end>` of non-overlapping half-open intervals `[begin, end)`. Overlapping and adjacent intervals are merged on insertion. This gives us:

- memory proportional to the number of "holes", not to the file size;
- the file is complete when `fileSize` bytes are covered. A counter is maintained so this check is O(1);
- checking that a range is fully received comes down to looking up a single interval in O(log n).

### Thread safety

Locking is two-level:

1. **A `std::shared_mutex` for the file map.** `OnNewChunk`, `ReadRange`, and `GetFile` take it in shared mode only for the duration of the lookup. Only `CollectFile` and `RemoveFile` take it exclusively.
2. **A `std::mutex` per file.** Threads working with different files never block each other.

Files are stored in the map as `std::shared_ptr<FileState>`. As a result, once the map lock is released, the file object is guaranteed to stay alive, even if it is concurrently removed via `RemoveFile`.

The map mutex is never acquired while a file mutex is held, so there is no deadlock.

### Parallel assembly of a single file

Data copying happens **without any lock**. Only interval operations are performed under the file mutex. Each file has two sets:

- `claimed`: bytes that some thread has already taken responsibility for writing, or has written;
- `received`: bytes that are definitely written.

`OnNewChunk` works in three phases:

1. **Under the mutex:** find the parts of the chunk that are not yet in `claimed` and claim them.
2. **Without the mutex:** copy only those parts.
3. **Under the mutex:** add them to `received` and check whether the file is complete.

- **every byte is written exactly once, by exactly one thread**: this is guaranteed by `claimed`, which is modified only under the mutex. Parallel writes therefore never touch the same bytes;
- overlapping regions and duplicates are not copied again;
- file completion and `ReadRange` look at `received`, so partially written data is never visible to the outside. The "write happens-before read" ordering is provided by the unlock/lock pair on the file mutex.

### Retrieving data

- **`ReadRange`** checks under the mutex that the range is in `received`, and then copies without locking: nobody writes to received bytes anymore. The caller allocates the memory, so a file can be read in pieces into the same buffer.
- **`GetFile`** returns a `std::shared_ptr<const std::vector<uint8_t>>`, i.e. the buffer itself, with no copy. This is safe: once the file is assembled, all of it is in `claimed`, so no thread writes to it anymore. The buffer lives as long as there are references to it, including after `RemoveFile`.

## Error handling

Methods return a `Status` instead of throwing exceptions. The reasons: `OnNewChunk` is called on the hot path, and invalid input (an unknown file, going out of bounds) is an expected situation in networking code, not an exceptional one.

| Situation | Result |
|---|---|
| `OnNewChunk` / `ReadRange` for an unregistered file | `UnknownFile` |
| Repeated `CollectFile` with the same id | `AlreadyExists` |
| `pos + size > fileSize` (with overflow checking) | `OutOfRange` |
| `ReadRange` for a range that has not been received yet | `NotReady` |
| A duplicate, a fully covered chunk, or an empty chunk | `Ok`: allowed by the specification |
| A chunk for an already assembled file | `Ok`, the buffer is not modified |

The signatures from the original skeleton were changed from `void` to `Status`. Existing code that ignores the result will keep working.

### Exception safety

Exceptions are possible only when memory runs out (`std::bad_alloc`, or `std::length_error` for a huge `fileSize`). The class does not catch them; they propagate to the caller.

- `CollectFile`: strong guarantee. The buffer is allocated before the map is modified.
- `IntervalSet::Add`: strong guarantee. The new node is allocated before any changes, and when intervals are merged an existing node is reused (`std::map::extract`) with no allocation. This matters: if intervals disappeared from `claimed` after an exception, two threads could end up writing to the same bytes.
- `OnNewChunk`: basic guarantee. There are no races or data corruption under any exception.
- `ReadRange`, `GetFile`, `RemoveFile` do not allocate and do not throw.

## Tests

- **`IntervalSetTest`**: merging of adjacent, overlapping, and nested intervals, absorbing several intervals at once, boundaries of `Covers` and `Missing`, plus a randomized comparison against a reference bitmap.
- **`FileCollectorTest`**: the example from the assignment, chunks in arbitrary order, duplicates, overlaps, a zero-size file, partial reads, removal, all error codes including `pos + size` overflow.
- **`FileCollectorConcurrencyTest`**: several threads assembling one file; all threads sending all chunks (maximum contention for the same bytes); parallel assembly of many files; reading while writing; simultaneous `CollectFile` with the same id; removal during writing.

By themselves, the multithreaded tests only verify that the result is correct. The absence of data races is demonstrated by running under ThreadSanitizer (`-DFC_ENABLE_TSAN=ON`).
