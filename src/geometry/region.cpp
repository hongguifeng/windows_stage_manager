#include "geometry/region.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace stage_manager::geometry {
namespace {

void sort_rectangles(std::vector<Rect>& rectangles)
{
    std::sort(rectangles.begin(), rectangles.end(), [](const auto& left, const auto& right) {
        if (left.top != right.top) {
            return left.top < right.top;
        }
        if (left.left != right.left) {
            return left.left < right.left;
        }
        if (left.bottom != right.bottom) {
            return left.bottom < right.bottom;
        }
        return left.right < right.right;
    });
}

bool try_merge(const Rect& left, const Rect& right, Rect& merged)
{
    if (left.top == right.top && left.bottom == right.bottom && left.right == right.left) {
        merged = {left.left, left.top, right.right, left.bottom};
        return true;
    }
    if (left.left == right.left && left.right == right.right && left.bottom == right.top) {
        merged = {left.left, left.top, left.right, right.bottom};
        return true;
    }
    return false;
}

void canonicalize(std::vector<Rect>& rectangles)
{
    bool changed = true;
    while (changed) {
        changed = false;
        sort_rectangles(rectangles);
        for (std::size_t left_index = 0; left_index < rectangles.size() && !changed;
             ++left_index) {
            for (std::size_t right_index = left_index + 1; right_index < rectangles.size();
                 ++right_index) {
                Rect merged;
                if (!try_merge(rectangles[left_index], rectangles[right_index], merged)) {
                    continue;
                }
                rectangles[left_index] = merged;
                rectangles.erase(rectangles.begin() + static_cast<std::ptrdiff_t>(right_index));
                changed = true;
                break;
            }
        }
    }
    sort_rectangles(rectangles);
}

void append_if_not_empty(std::vector<Rect>& output, const Rect& rectangle)
{
    if (!rectangle.empty()) {
        output.push_back(rectangle);
    }
}

std::vector<Rect> subtract_rectangle(const Rect& source, const Rect& blocker)
{
    const auto overlap = source.intersection(blocker);
    if (!overlap) {
        return {source};
    }

    std::vector<Rect> result;
    result.reserve(4);
    append_if_not_empty(result, {source.left, source.top, source.right, overlap->top});
    append_if_not_empty(result, {source.left, overlap->bottom, source.right, source.bottom});
    append_if_not_empty(result, {source.left, overlap->top, overlap->left, overlap->bottom});
    append_if_not_empty(result, {overlap->right, overlap->top, source.right, overlap->bottom});
    return result;
}

RegionOperationResult make_result(std::vector<Rect> rectangles, std::size_t maximum_rectangles)
{
    canonicalize(rectangles);
    if (rectangles.size() > maximum_rectangles) {
        return {RegionStatus::TooComplex, {}};
    }
    auto region = Region::from_disjoint(rectangles, maximum_rectangles);
    if (!region) {
        return {RegionStatus::InvalidInput, {}};
    }
    return {RegionStatus::Ok, std::move(*region)};
}

RegionOperationResult subtract_rectangles(std::vector<Rect> pieces,
                                          std::span<const Rect> blockers,
                                          std::size_t maximum_rectangles)
{
    for (const auto& blocker : blockers) {
        std::vector<Rect> next;
        for (const auto& piece : pieces) {
            auto fragments = subtract_rectangle(piece, blocker);
            next.insert(next.end(), fragments.begin(), fragments.end());
        }
        canonicalize(next);
        if (next.size() > maximum_rectangles) {
            return {RegionStatus::TooComplex, {}};
        }
        pieces = std::move(next);
        if (pieces.empty()) {
            break;
        }
    }
    return make_result(std::move(pieces), maximum_rectangles);
}

} // namespace

std::optional<Region> Region::from_disjoint(
    std::span<const Rect> rectangles, std::size_t maximum_rectangles)
{
    Region region;
    region.rectangles_.reserve(std::min(rectangles.size(), maximum_rectangles));
    for (const auto& rectangle : rectangles) {
        if (!rectangle.valid()) {
            return std::nullopt;
        }
        if (rectangle.empty()) {
            continue;
        }
        if (std::any_of(region.rectangles_.begin(), region.rectangles_.end(),
                        [&rectangle](const auto& existing) {
                            return existing.intersects(rectangle);
                        })) {
            return std::nullopt;
        }
        region.rectangles_.push_back(rectangle);
        canonicalize(region.rectangles_);
        if (region.rectangles_.size() > maximum_rectangles) {
            return std::nullopt;
        }
    }

    canonicalize(region.rectangles_);
    return region;
}

const std::vector<Rect>& Region::rectangles() const noexcept
{
    return rectangles_;
}

bool Region::empty() const noexcept
{
    return rectangles_.empty();
}

std::size_t Region::size() const noexcept
{
    return rectangles_.size();
}

std::uint64_t Region::area() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& rectangle : rectangles_) {
        const auto width = static_cast<std::uint64_t>(rectangle.width());
        const auto height = static_cast<std::uint64_t>(rectangle.height());
        if (height != 0 && width > std::numeric_limits<std::uint64_t>::max() / height) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const auto rectangle_area = width * height;
        if (total > std::numeric_limits<std::uint64_t>::max() - rectangle_area) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += rectangle_area;
    }
    return total;
}

std::optional<Rect> Region::bounds() const noexcept
{
    if (rectangles_.empty()) {
        return std::nullopt;
    }
    Rect result = rectangles_.front();
    for (const auto& rectangle : rectangles_) {
        result.left = std::min(result.left, rectangle.left);
        result.top = std::min(result.top, rectangle.top);
        result.right = std::max(result.right, rectangle.right);
        result.bottom = std::max(result.bottom, rectangle.bottom);
    }
    return result;
}

RegionOperationResult unite(const Region& left,
                            const Region& right,
                            std::size_t maximum_rectangles)
{
    std::vector<Rect> result = left.rectangles();
    if (result.size() > maximum_rectangles) {
        return {RegionStatus::TooComplex, {}};
    }
    for (const auto& rectangle : right.rectangles()) {
        const auto uncovered = subtract_rectangles(
            {rectangle}, std::span<const Rect>(result), maximum_rectangles);
        if (!uncovered.succeeded()) {
            return uncovered;
        }
        result.insert(result.end(),
                      uncovered.region.rectangles().begin(),
                      uncovered.region.rectangles().end());
        canonicalize(result);
        if (result.size() > maximum_rectangles) {
            return {RegionStatus::TooComplex, {}};
        }
    }
    return make_result(std::move(result), maximum_rectangles);
}

RegionOperationResult subtract(const Region& source,
                               const Region& blockers,
                               std::size_t maximum_rectangles)
{
    return subtract_rectangles(source.rectangles(), blockers.rectangles(), maximum_rectangles);
}

RegionOperationResult intersect(const Region& left,
                                const Region& right,
                                std::size_t maximum_rectangles)
{
    std::vector<Rect> intersections;
    for (const auto& left_rectangle : left.rectangles()) {
        for (const auto& right_rectangle : right.rectangles()) {
            const auto overlap = left_rectangle.intersection(right_rectangle);
            if (!overlap) {
                continue;
            }
            intersections.push_back(*overlap);
        }
    }
    return make_result(std::move(intersections), maximum_rectangles);
}

} // namespace stage_manager::geometry
