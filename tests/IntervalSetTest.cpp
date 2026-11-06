#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include <vector>

#include "IntervalSet.hpp"

using Ranges = std::vector<IntervalSet::Range>;

TEST(IntervalSetTest, MissingOnEmptySet) {
    IntervalSet s;
    EXPECT_EQ(s.Missing(2, 5), (Ranges{{2, 5}}));
    EXPECT_TRUE(s.Missing(5, 5).empty());
}

TEST(IntervalSetTest, MissingWithGaps) {
    IntervalSet s;
    s.Add(0, 2);
    s.Add(4, 6);
    s.Add(8, 10);
    EXPECT_EQ(s.Missing(1, 9), (Ranges{{2, 4}, {6, 8}}));
    EXPECT_EQ(s.Missing(0, 12), (Ranges{{2, 4}, {6, 8}, {10, 12}}));
    EXPECT_EQ(s.Missing(3, 5), (Ranges{{3, 4}}));
    EXPECT_TRUE(s.Missing(4, 6).empty());
}

TEST(IntervalSetTest, EmptySet) {
    IntervalSet s;
    EXPECT_EQ(s.Covered(), 0u);
    EXPECT_FALSE(s.Covers(0, 1));
    EXPECT_TRUE(s.Covers(5, 5));
}

TEST(IntervalSetTest, SingleIntervalBoundaries) {
    IntervalSet s;
    s.Add(2, 6);
    EXPECT_EQ(s.Covered(), 4u);
    EXPECT_TRUE(s.Covers(2, 6));
    EXPECT_TRUE(s.Covers(3, 5));
    EXPECT_FALSE(s.Covers(2, 7));
    EXPECT_FALSE(s.Covers(1, 6));
    EXPECT_FALSE(s.Covers(0, 2));
    EXPECT_FALSE(s.Covers(6, 7));
}

TEST(IntervalSetTest, DegenerateAddIsIgnored) {
    IntervalSet s;
    s.Add(5, 5);
    s.Add(7, 3);
    EXPECT_EQ(s.Covered(), 0u);
    EXPECT_FALSE(s.Covers(3, 7));
}

TEST(IntervalSetTest, DisjointWithGap) {
    IntervalSet s;
    s.Add(0, 4);
    s.Add(6, 10);
    EXPECT_EQ(s.Covered(), 8u);
    EXPECT_TRUE(s.Covers(0, 4));
    EXPECT_TRUE(s.Covers(6, 10));
    EXPECT_FALSE(s.Covers(0, 10));
    EXPECT_FALSE(s.Covers(3, 7));
    EXPECT_FALSE(s.Covers(4, 6));
}

TEST(IntervalSetTest, AdjacentIntervalsMerge) {
    IntervalSet s;
    s.Add(0, 4);
    s.Add(4, 8);
    EXPECT_EQ(s.Covered(), 8u);
    EXPECT_TRUE(s.Covers(0, 8));
}

TEST(IntervalSetTest, AdjacentIntervalsMergeReversed) {
    IntervalSet s;
    s.Add(4, 8);
    s.Add(0, 4);
    EXPECT_EQ(s.Covered(), 8u);
    EXPECT_TRUE(s.Covers(0, 8));
}

TEST(IntervalSetTest, OverlappingIntervals) {
    IntervalSet s;
    s.Add(0, 4);
    s.Add(2, 6);
    EXPECT_EQ(s.Covered(), 6u);
    EXPECT_TRUE(s.Covers(0, 6));
}

TEST(IntervalSetTest, NestedIntervalDoesNotChangeCoverage) {
    IntervalSet s;
    s.Add(0, 10);
    s.Add(3, 5);
    EXPECT_EQ(s.Covered(), 10u);
    EXPECT_TRUE(s.Covers(0, 10));
}

TEST(IntervalSetTest, EnclosingIntervalAbsorbsOld) {
    IntervalSet s;
    s.Add(3, 5);
    s.Add(0, 10);
    EXPECT_EQ(s.Covered(), 10u);
    EXPECT_TRUE(s.Covers(0, 10));
}

TEST(IntervalSetTest, AbsorbsSeveralIntervals) {
    IntervalSet s;
    s.Add(0, 2);
    s.Add(4, 6);
    s.Add(8, 10);
    s.Add(1, 9);
    EXPECT_EQ(s.Covered(), 10u);
    EXPECT_TRUE(s.Covers(0, 10));
}

TEST(IntervalSetTest, DuplicateAdd) {
    IntervalSet s;
    s.Add(0, 4);
    s.Add(0, 4);
    EXPECT_EQ(s.Covered(), 4u);
}

TEST(IntervalSetTest, ReverseOrderInsertion) {
    IntervalSet s;
    for (size_t i = 10; i > 0; --i) {
        s.Add(i - 1, i);
    }
    EXPECT_EQ(s.Covered(), 10u);
    EXPECT_TRUE(s.Covers(0, 10));
}

TEST(IntervalSetTest, FillingGap) {
    IntervalSet s;
    s.Add(0, 4);
    s.Add(6, 10);
    s.Add(4, 6);
    EXPECT_EQ(s.Covered(), 10u);
    EXPECT_TRUE(s.Covers(0, 10));
}

TEST(IntervalSetTest, RandomAgainstBitmap) {
    constexpr size_t kSize = 200;
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, kSize);

    for (int round = 0; round < 50; ++round) {
        IntervalSet s;
        std::vector<bool> bits(kSize, false);

        for (int i = 0; i < 30; ++i) {
            size_t a = dist(rng);
            size_t b = dist(rng);
            if (a > b) {
                std::swap(a, b);
            }
            s.Add(a, b);
            for (size_t k = a; k < b; ++k) {
                bits[k] = true;
            }

            ASSERT_EQ(s.Covered(),
                      static_cast<size_t>(std::count(bits.begin(), bits.end(), true)));

            size_t c = dist(rng), d = dist(rng);
            if (c > d) {
                std::swap(c, d);
            }
            const bool expected =
                std::all_of(bits.begin() + c, bits.begin() + d,
                    [](bool x) { return x; });
            ASSERT_EQ(s.Covers(c, d), expected) << "range [" << c << ", " << d << ")";

            Ranges expectedGaps;
            for (size_t k = c; k < d; ++k) {
                if (bits[k]) continue;
                if (!expectedGaps.empty() && expectedGaps.back().second == k) {
                    ++expectedGaps.back().second;
                } else {
                    expectedGaps.emplace_back(k, k + 1);
                }
            }
            ASSERT_EQ(s.Missing(c, d), expectedGaps) << "range [" << c << ", " << d << ")";
        }
    }
}