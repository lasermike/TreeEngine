#pragma once

#include <cstddef>

enum class TreeSegment
{
    BranchLOD0,
    BranchLOD1,
    LeafLOD0,
    Count
};

constexpr size_t TreeSegmentCount = static_cast<size_t>(TreeSegment::Count);

constexpr size_t TreeSegmentIndex(TreeSegment segment)
{
    return static_cast<size_t>(segment);
}
