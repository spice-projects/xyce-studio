#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "decimate.h"

// ---------------------------------------------------------------------------
// index-selection helpers (value-dependent algorithms)
// each helper fills *out_indices* with sorted indices into the original vector.
// separating index computation from value selection lets the same index set be
// applied to both the abscissa and ordinate in decimate_xy().
// ---------------------------------------------------------------------------

// ensure *indices* contains at most *target* entries while keeping both endpoints.
// if the vector is already short enough it is left unchanged.  when trimming is
// required the first and last indices are always kept and the interior is sampled evenly.
static void trim_indices(std::vector<size_t>& indices, size_t target) {
    // nothing to do if already within budget
    if (indices.size() <= target)
        return;
    // degenerate case: keep at most the two endpoints
    if (target <= 2) {
        size_t first = indices.front();
        size_t last = indices.back();
        indices.clear();
        if (target >= 1)
            indices.push_back(first);
        if (target == 2)
            indices.push_back(last);
        return;
    }
    // sample the interior evenly, always preserving the first and last index
    size_t first = indices.front();
    size_t last = indices.back();
    // number of interior slots we can fill
    size_t count = target - 2;
    size_t interior = indices.size() - 2;
    std::vector<size_t> result;
    result.reserve(target);
    result.push_back(first);
    // pick *count* evenly spaced positions across the interior
    for (size_t i = 0; i < count; ++i) {
        // use rounding to match the Python linspace-based selection;
        // when count == 1 there is no valid denominator so always pick the middle element
        size_t pos = (count == 1) ? (interior / 2) : static_cast<size_t>(std::lround(static_cast<double>(i) * static_cast<double>(interior - 1) / static_cast<double>(count - 1)));
        result.push_back(indices[1 + pos]);
    }
    result.push_back(last);
    indices = std::move(result);
}

// produce at most *target* evenly-spaced indices covering [0, length-1].
// mirrors the Python _nth_point_indices() implementation.
static std::vector<size_t> nth_point_indices(size_t length, size_t target) {
    // short-circuit when the input is already smaller than the target
    if (length <= target) {
        std::vector<size_t> all(length);
        std::iota(all.begin(), all.end(), 0u);
        return all;
    }
    // generate *target* evenly-spaced float positions then round to integers,
    // removing duplicates that arise when target is close to length
    std::vector<size_t> indices;
    indices.reserve(target);
    for (size_t i = 0; i < target; ++i) {
        // linspace: 0 .. length-1 in *target* steps
        size_t idx = static_cast<size_t>(std::lround(static_cast<double>(i) * static_cast<double>(length - 1) / static_cast<double>(target - 1)));
        // deduplicate (can occur when target ≈ length)
        if (indices.empty() || indices.back() != idx)
            indices.push_back(idx);
    }
    return indices;
}

// retain the index of the minimum and maximum value inside each bucket.
// mirrors the Python _min_max_indices() implementation.
static std::vector<size_t> min_max_indices(const std::span<const double>& values, size_t target) {
    size_t length = values.size();
    // trivial small-target cases
    if (target <= 1)
        return {0};
    if (target == 2)
        return {0, length - 1};
    // size each bucket so that two points per bucket (min & max) do not exceed target
    size_t half = std::max<size_t>(1, target / 2);
    size_t points_per_bucket = std::max<size_t>(1, (length + half - 1) / half);
    size_t number_of_buckets = length / points_per_bucket;
    // collect min and max indices for each bucket
    std::vector<size_t> indices;
    indices.reserve(number_of_buckets * 2);
    for (size_t b = 0; b < number_of_buckets; ++b) {
        size_t bucket_start = b * points_per_bucket;
        // find the position of the min and max within the bucket
        auto bucket = values.subspan(bucket_start, points_per_bucket);
        size_t min_rel = static_cast<size_t>(std::min_element(bucket.begin(), bucket.end()) - bucket.begin());
        size_t max_rel = static_cast<size_t>(std::max_element(bucket.begin(), bucket.end()) - bucket.begin());
        // push both absolute indices — interleave min then max like the Python version
        indices.push_back(bucket_start + min_rel);
        indices.push_back(bucket_start + max_rel);
    }
    // sort and deduplicate
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    // ensure we do not exceed the target after deduplication
    trim_indices(indices, target);
    return indices;
}

// extend min-max by also keeping the first and last sample of every bucket (4 points per bucket)
static std::vector<size_t> m4_indices(const std::span<const double>& values, size_t target) {
    // capture the length of the input vector
    size_t length = values.size();
    // trivial small-target cases
    if (target <= 1)
        return {0};
    if (target == 2)
        return {0, length - 1};
    // size each bucket so that four points per bucket stay within target
    size_t quarter = std::max<size_t>(1, target / 4);
    size_t points_per_bucket = std::max<size_t>(1, (length + quarter - 1) / quarter);
    size_t number_of_buckets = length / points_per_bucket;
    // collect first, last, min, max indices for each bucket
    std::vector<size_t> indices;
    indices.reserve(number_of_buckets * 4);
    // loop over buckets
    for (size_t b = 0; b < number_of_buckets; ++b) {
        // start of the bucket in the original vector
        size_t bucket_start = b * points_per_bucket;
        auto bucket = values.subspan(bucket_start, points_per_bucket);
        // relative positions of the first, last, min, and max within the bucket
        size_t first_rel = 0;
        size_t last_rel = points_per_bucket - 1;
        size_t min_rel = static_cast<size_t>(std::min_element(bucket.begin(), bucket.end()) - bucket.begin());
        size_t max_rel = static_cast<size_t>(std::max_element(bucket.begin(), bucket.end()) - bucket.begin());
        // accumulate all four candidates
        indices.push_back(bucket_start + first_rel);
        indices.push_back(bucket_start + last_rel);
        indices.push_back(bucket_start + min_rel);
        indices.push_back(bucket_start + max_rel);
    }
    // sort and deduplicate
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    // ensure we do not exceed the target after deduplication
    trim_indices(indices, target);
    // exit
    return indices;
}

// largest triangle three buckets — for each output point pick the sample in
// the current bucket that forms the triangle with the largest area relative to
// the previously selected point and the centroid of the next bucket.
// mirrors the Python _lttb_indices() implementation.
static std::vector<size_t> lttb_indices(const std::span<const double>& x, const std::span<const double>& values, size_t target) {
    size_t length = values.size();
    // handle degenerate target values explicitly
    if (target == 0)
        return {};
    if (target == 1)
        return {0};
    // output buffer; first and last slots are fixed
    std::vector<size_t> out_indices(target);
    out_indices.front() = 0;
    out_indices.back() = length - 1;
    // target == 2 is already satisfied
    if (target == 2)
        return out_indices;
    // divide the interior into (target - 2) buckets; each iteration selects one point
    double bucket_size = static_cast<double>(length - 2) / static_cast<double>(target - 2);
    // previously selected index (starts at the first sample)
    size_t prev_index = 0;
    for (size_t i = 1; i < target - 1; ++i) {
        // boundaries of the current bucket
        size_t bucket_start = static_cast<size_t>((i - 1) * bucket_size) + 1;
        size_t bucket_end = std::min(static_cast<size_t>(i * bucket_size) + 1, length - 1);
        // boundaries of the next bucket (used to compute its centroid)
        size_t next_start = bucket_end;
        size_t next_end = std::min(static_cast<size_t>((i + 1) * bucket_size) + 1, length - 1);
        // centroid of the next bucket — the "far" apex of the triangle
        double next_x_sum = 0.0;
        double next_y_sum = 0.0;
        size_t next_count = next_end - next_start;
        for (size_t j = next_start; j < next_end; ++j) {
            next_x_sum += x[j];
            next_y_sum += values[j];
        }
        double next_x_avg = (next_count > 0) ? (next_x_sum / static_cast<double>(next_count)) : x[next_start];
        double next_y_avg = (next_count > 0) ? (next_y_sum / static_cast<double>(next_count)) : values[next_start];
        // point A: the previously selected sample
        double ax = x[prev_index];
        double ay = values[prev_index];
        // find the candidate in the current bucket with the largest triangle area
        size_t best_relative = 0;
        double best_area = -1.0;
        for (size_t j = bucket_start; j < bucket_end; ++j) {
            double bx = x[j];
            double by = values[j];
            // triangle area = 0.5 * |cross product|; the 0.5 is constant so we
            // maximise the absolute cross product
            double area = std::abs((ax - next_x_avg) * (by - ay) - (ax - bx) * (next_y_avg - ay));
            if (area > best_area) {
                best_area = area;
                best_relative = j - bucket_start;
            }
        }
        size_t best_index = bucket_start + best_relative;
        out_indices[i] = best_index;
        prev_index = best_index;
    }
    return out_indices;
}

// perpendicular distance from point (px, py) to the line from (x0, y0) to (x1, y1).
// used by the iterative RDP helper.
static double point_to_segment_distance(double px, double py, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0;
    double dy = y1 - y0;
    // degenerate segment: both endpoints are the same point
    if (dx == 0.0 && dy == 0.0)
        return std::hypot(px - x0, py - y0);
    double denom = std::hypot(dx, dy);
    return std::abs((px - x0) * dy - (py - y0) * dx) / denom;
}

// run one pass of RDP at the given epsilon and return the keep-mask.
// mirrors the Python _rdp_indices_for_epsilon() implementation.
static std::vector<size_t> rdp_indices_for_epsilon(const std::span<const double>& x, const std::span<const double>& values, double epsilon) {
    size_t length = values.size();
    // initialize keep mask — always preserve endpoints
    std::vector<bool> keep(length, false);
    keep[0] = true;
    keep[length - 1] = true;
    // iterative segment stack — avoids recursion-depth limits for large inputs
    std::vector<std::pair<size_t, size_t>> stack;
    stack.push_back({0, length - 1});
    while (!stack.empty()) {
        // pop one segment
        auto [start, end] = stack.back();
        stack.pop_back();
        // segments with no interior points are already simplified
        if (end <= start + 1)
            continue;
        // find the farthest interior point from the segment line
        double x0 = x[start];
        double y0 = values[start];
        double x1 = x[end];
        double y1 = values[end];
        double max_distance = -1.0;
        size_t farthest_index = start + 1;
        for (size_t i = start + 1; i < end; ++i) {
            double d = point_to_segment_distance(x[i], values[i], x0, y0, x1, y1);
            if (d > max_distance) {
                max_distance = d;
                farthest_index = i;
            }
        }
        // keep and split only when the tolerance is exceeded
        if (max_distance > epsilon) {
            keep[farthest_index] = true;
            stack.push_back({start, farthest_index});
            stack.push_back({farthest_index, end});
        }
    }
    // collect kept indices in ascending order
    std::vector<size_t> indices;
    indices.reserve(length);
    for (size_t i = 0; i < length; ++i)
        if (keep[i])
            indices.push_back(i);
    return indices;
}

// auto-tune the RDP epsilon via binary search so the output has at most
// *target* points.  mirrors the Python _rdp_indices() implementation.
static std::vector<size_t> rdp_indices(const std::span<const double>& x, const std::span<const double>& values, size_t target) {
    size_t length = values.size();
    // explicit tiny-target handling keeps behavior aligned with other algorithms
    if (target == 0)
        return {};
    if (target == 1)
        return {0};
    if (target == 2)
        return {0, length - 1};
    if (length <= target) {
        std::vector<size_t> all(length);
        std::iota(all.begin(), all.end(), 0u);
        return all;
    }
    // compute the maximum possible perpendicular distance (epsilon upper bound)
    double x0 = x[0];
    double y0 = values[0];
    double x1 = x[length - 1];
    double y1 = values[length - 1];
    double epsilon_low = 0.0;
    double epsilon_high = 0.0;
    for (size_t i = 0; i < length; ++i) {
        double d = point_to_segment_distance(x[i], values[i], x0, y0, x1, y1);
        if (d > epsilon_high)
            epsilon_high = d;
    }
    // binary-search epsilon so output length is as close as possible without exceeding target
    std::vector<size_t> best = rdp_indices_for_epsilon(x, values, epsilon_high);
    for (int iter = 0; iter < 24; ++iter) {
        double epsilon_mid = (epsilon_low + epsilon_high) / 2.0;
        auto candidate = rdp_indices_for_epsilon(x, values, epsilon_mid);
        if (candidate.size() > target)
            epsilon_low = epsilon_mid;
        else {
            best = candidate;
            epsilon_high = epsilon_mid;
        }
    }
    // final guard: trim safely if binary search lands below target due to discrete jumps
    trim_indices(best, target);
    return best;
}

// ---------------------------------------------------------------------------
// public entry point
// ---------------------------------------------------------------------------

std::pair<View<double>, View<double>> decimate_xy(const std::span<const double>& abscissa_values, const std::span<const double>& ordinate_values, size_t decimate_target, int algorithm) {
    // validate that both arrays are the same length
    if (abscissa_values.size() != ordinate_values.size())
        throw std::invalid_argument("abscissa_values and ordinate_values must have the same length");
    // validate target — NONE bypasses decimation so any value is accepted
    if (decimate_target < 1 && algorithm != DECIMATE_NONE)
        throw std::invalid_argument("decimate_target must be >= 1");
    // NONE — pass both arrays through unchanged
    if (algorithm == DECIMATE_NONE)
        return {View(abscissa_values.data(), abscissa_values.size()), View(ordinate_values.data(), ordinate_values.size())};
    // vector length
    size_t length = ordinate_values.size();
    if (length <= decimate_target)
        return {View(abscissa_values.data(), abscissa_values.size()), View(ordinate_values.data(), ordinate_values.size())};
    // index-based algorithms — derive indices from y, apply to both x and y
    std::vector<size_t> indices;
    // process algorithm-specific index selection
    if (algorithm == DECIMATE_NTH_POINT) {
        // value-independent stride; x and y stay coherent naturally
        indices = nth_point_indices(length, decimate_target);
    }
    else if (algorithm == DECIMATE_MIN_MAX) {
        // derive index set from y (ordinate) values
        indices = min_max_indices(ordinate_values, decimate_target);
    }
    else if (algorithm == DECIMATE_M4) {
        // derive index set from y (ordinate) values
        indices = m4_indices(ordinate_values, decimate_target);
    }
    else if (algorithm == DECIMATE_LTTB) {
        // pass real x axis so triangle areas are computed in chart space
        indices = lttb_indices(abscissa_values, ordinate_values, decimate_target);
    }
    else if (algorithm == DECIMATE_RDP) {
        // use the real x axis so perpendicular distances are in chart space
        indices = rdp_indices(abscissa_values, ordinate_values, decimate_target);
    }
    else {
        // unknown algorithm
        throw std::invalid_argument("unknown decimation algorithm");
    }
    // return vectors
    std::vector<double> x_out;
    std::vector<double> y_out;
    // allocate space and populate
    x_out.reserve(indices.size());
    y_out.reserve(indices.size());
    // copy the selected values from the original arrays
    for (size_t i = 0; i < indices.size(); ++i) {
        // copy value at index
        x_out.emplace_back(abscissa_values[indices[i]]);
        y_out.emplace_back(ordinate_values[indices[i]]);
    }
    // exit
    return {View(std::move(x_out)), View(std::move(y_out))};
}
