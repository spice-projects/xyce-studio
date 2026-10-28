#pragma once

#include <span>
#include <tuple>

#include "expression/expression.h"

// ---------------------------------------------------------------------------
// algorithm selector constants — mirror the Python DecimationAlgorithm enum
// ---------------------------------------------------------------------------

// pass the original vectors through unchanged (decimation disabled)
inline constexpr int DECIMATE_NONE = 0;
// keep every N-th sample using evenly-spaced linspace indices
inline constexpr int DECIMATE_NTH_POINT = 1;
// retain the index of the min and max value inside each bucket
inline constexpr int DECIMATE_MIN_MAX = 2;
// extend min-max by also keeping the first and last sample of every bucket (4 points per bucket)
inline constexpr int DECIMATE_M4 = 3;
// largest triangle three buckets — maximises perceived visual fidelity for smooth signals
inline constexpr int DECIMATE_LTTB = 4;
// ramer-douglas-peucker polyline simplification with auto-tuned epsilon
inline constexpr int DECIMATE_RDP = 5;

// decimate an (x, y) pair jointly so plotted points remain coherent.
// for value-dependent algorithms (MIN_MAX, M4, LTTB, RDP) the index set is
// derived from the y values and then applied to x as well, guaranteeing that
// every output (x[i], y[i]) pair maps to the same original sample point.
// for AVERAGE, both arrays are bucketed with the same boundaries and each
// output point is the arithmetic mean of its bucket.
// for NTH_POINT the stride is value-independent so x and y are naturally coherent.
// when len(y) <= decimate_target, both input spans are returned unchanged.
// raises std::invalid_argument when len(x) != len(y) or decimate_target < 1 (unless algorithm is NONE).
std::pair<View<double>, View<double>> decimate_xy(const std::span<const double>& abscissa_values, const std::span<const double>& ordinate_values, size_t decimate_target, int algorithm);
