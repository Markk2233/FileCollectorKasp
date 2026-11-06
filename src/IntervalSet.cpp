#include "IntervalSet.hpp"

#include <iterator>
#include <algorithm>

void IntervalSet::Add(size_t begin, size_t end) {
    if (begin >= end) {
        return;
    }

    auto first = intervals_.upper_bound(begin);
    if (first != intervals_.begin()) {
        auto prev = std::prev(first);
        if (prev->second >= begin) {
            first = prev;
        }
    }

    auto last = first;
    size_t removed = 0;
    while (last != intervals_.end() && last->first <= end) {
        begin = std::min(begin, last->first);
        end = std::max(end, last->second);
        removed += last->second - last->first;
        ++last;
    }

    if (first == last) {
        intervals_.emplace_hint(last, begin, end);
    }
    else {
        auto next = std::next(first);
        auto node = intervals_.extract(first);
        intervals_.erase(next, last);
        node.key() = begin;
        node.mapped() = end;
        intervals_.insert(std::move(node));
    }
    covered_ = covered_ - removed + (end - begin);
}

bool IntervalSet::Covers(size_t begin, size_t end) const {
    if (begin >= end) {
        return true;
    }
    auto it = intervals_.upper_bound(begin);
    if (it == intervals_.begin()) {
        return false;
    }
    --it;
    return it->second >= end;
}

std::vector<IntervalSet::Range> IntervalSet::Missing(size_t begin, size_t end) const {
    std::vector<Range> result;
    if (begin >= end) {
        return result;
    }

    size_t cur = begin;
    auto it = intervals_.upper_bound(begin);
    if (it != intervals_.begin()) {
        cur = std::max(cur, std::prev(it)->second);
    }

    while (cur < end && it != intervals_.end() && it->first < end) {
        if (it->first > cur) {
            result.emplace_back(cur, it->first);
        }
        cur = std::max(cur, it->second);
        ++it;
    }

    if (cur < end) {
        result.emplace_back(cur, end);
    }
    return result;
}

size_t IntervalSet::Covered() const {
    return covered_;
}
