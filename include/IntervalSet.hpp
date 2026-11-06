#pragma once

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

class IntervalSet {
public:
    using Range = std::pair<size_t, size_t>;

    void Add(size_t begin, size_t end);
    [[nodiscard]] bool Covers(size_t begin, size_t end) const;
    [[nodiscard]] std::vector<Range> Missing(size_t begin, size_t end) const;
    [[nodiscard]] size_t Covered() const;

private:
    std::map<size_t, size_t> intervals_;
    size_t covered_ = 0;
};