#include "sim/physics.hpp"

#include <algorithm>
#include <cmath>

namespace strefa::sim::detail {
void CompactPressure::allocate() {
    index.resize(grid_cells);
    rows.reserve(grid_cells + 1);
    for (auto* a : {&x, &b, &r, &z, &direction, &product}) a->reserve(grid_cells + 1);
    coarse.group.reserve(grid_cells + 1);
    coarse.factor.reserve(40 * 24 * 41);
    coarse.rhs.reserve(40 * 24);
    coarse.solution.reserve(40 * 24);
}
namespace {
void build_coarse(CompactPressure& p, const LiquidFields& l, int count) {
    auto& c = p.coarse;
    c.ready = false;
    c.count = 0;
    c.band = 0;
    constexpr int block = 8, columns = grid_width / block, groups = columns * (grid_height / block);
    std::array<int, groups> indices{};
    indices.fill(-1);
    c.group.resize(count + 1);
    c.group[0] = -1;
    for (int n = 1; n <= count; ++n) {
        const int i = l.wet[n - 1];
        if (l.anchor[i] == 2) {
            c.group[n] = -1;
            continue;
        }
        const int spatial = (i / grid_width / block) * columns + i % grid_width / block;
        indices[spatial] = 0;
        c.group[n] = spatial;
    }
    for (int& index : indices)
        if (index >= 0) index = c.count++;
    for (int n = 1; n <= count; ++n)
        if (c.group[n] >= 0) c.group[n] = indices[c.group[n]];
    if (c.count == 0) return;
    for (int n = 1; n <= count; ++n) {
        if (c.group[n] < 0) continue;
        const auto& row = p.rows[n];
        for (const int j : {row.left, row.right, row.up, row.down})
            if (j && c.group[j] >= 0) c.band = std::max(c.band, std::abs(c.group[n] - c.group[j]));
    }
    const int stride = c.band + 1;
    c.factor.assign(c.count * stride, 0.0);
    c.rhs.resize(c.count);
    c.solution.resize(c.count);
    auto entry = [&](int i, int j) -> double& { return c.factor[i * stride + i - j]; };
    // Galerkin A_c=P^T A P, piecewise constant aggregates. Only the
    // preconditioner is coarse: thin walls and free surfaces remain in fine A.
    for (int n = 1; n <= count; ++n) {
        const int g = c.group[n];
        if (g < 0) continue;
        const auto& row = p.rows[n];
        entry(g, g) += row.diagonal;
        auto edge = [&](int neighbor, double value) {
            if (!neighbor || c.group[neighbor] < 0) return;
            const int h = c.group[neighbor];
            if (g == h)
                entry(g, g) -= value;
            else if (g > h)
                entry(g, h) -= value;
        };
        edge(row.left, row.a_left);
        edge(row.right, row.a_right);
        edge(row.up, row.a_up);
        edge(row.down, row.a_down);
    }
    // The small coarse operator is SPD; exact banded Cholesky keeps this
    // additive preconditioner linear and SPD, unlike a tolerance-driven inner CG.
    for (int i = 0; i < c.count; ++i) {
        for (int j = std::max(0, i - c.band); j < i; ++j) {
            double value = entry(i, j);
            for (int k = std::max(0, i - c.band); k < j; ++k) value -= entry(i, k) * entry(j, k);
            entry(i, j) = value / entry(j, j);
        }
        double diagonal = entry(i, i);
        for (int k = std::max(0, i - c.band); k < i; ++k) diagonal -= entry(i, k) * entry(i, k);
        if (!(diagonal > 1e-12) || !std::isfinite(diagonal)) return; // Fall back to MIC.
        entry(i, i) = std::sqrt(diagonal);
    }
    c.ready = true;
}
void coarse_correction(CompactPressure& p, int count) {
    auto& c = p.coarse;
    if (!c.ready) return;
    std::ranges::fill(c.rhs, 0.0);
    for (int i = 1; i <= count; ++i)
        if (c.group[i] >= 0) c.rhs[c.group[i]] += p.r[i];
    const int stride = c.band + 1;
    auto entry = [&](int i, int j) { return c.factor[i * stride + i - j]; };
    for (int i = 0; i < c.count; ++i) {
        double value = c.rhs[i];
        for (int j = std::max(0, i - c.band); j < i; ++j) value -= entry(i, j) * c.solution[j];
        c.solution[i] = value / entry(i, i);
    }
    for (int i = c.count - 1; i >= 0; --i) {
        double value = c.solution[i];
        for (int j = i + 1; j < std::min(c.count, i + c.band + 1); ++j) value -= entry(j, i) * c.solution[j];
        c.solution[i] = value / entry(i, i);
    }
    for (int i = 1; i <= count; ++i)
        if (c.group[i] >= 0) p.z[i] += c.solution[c.group[i]];
}
double dot(const std::vector<double>& a, const std::vector<double>& b, int count) {
    double sum = 0;
    for (int i = 1; i <= count; ++i) sum += a[i] * b[i];
    return sum;
}
void multiply(const CompactPressure& p, const std::vector<double>& x, std::vector<double>& result, int count) {
    for (int i = 1; i <= count; ++i) {
        const auto& row = p.rows[i];
        double value = row.diagonal * x[i];
        value -= row.a_left * x[row.left];
        value -= row.a_right * x[row.right];
        value -= row.a_up * x[row.up];
        value -= row.a_down * x[row.down];
        result[i] = value;
    }
}
void precondition(CompactPressure& p, int count, bool modified) {
    if (modified) {
        // Equivalent LDL^T form of MIC. The backward sweep needs one product
        // per edge rather than repeatedly multiplying by sqrt(D)^-1.
        for (int i = 1; i <= count; ++i) {
            const auto& row = p.rows[i];
            double value = p.r[i];
            value += row.a_left * p.rows[row.left].inverse_factor * p.z[row.left];
            value += row.a_up * p.rows[row.up].inverse_factor * p.z[row.up];
            p.z[i] = value;
        }
        for (int i = count; i > 0; --i) {
            const auto& row = p.rows[i];
            p.z[i] = (p.z[i] + row.a_right * p.z[row.right] + row.a_down * p.z[row.down]) * row.inverse_factor;
        }
        coarse_correction(p, count);
        return;
    }
    for (int i = 1; i <= count; ++i) {
        const auto& row = p.rows[i];
        double value = p.r[i];
        value += row.a_left * p.rows[row.left].inverse_factor * p.z[row.left];
        value += row.a_up * p.rows[row.up].inverse_factor * p.z[row.up];
        p.z[i] = value * row.inverse_factor;
    }
    for (int i = count; i > 0; --i) {
        const auto& row = p.rows[i];
        double value = p.z[i];
        value += row.a_right * row.inverse_factor * p.z[row.right];
        value += row.a_down * row.inverse_factor * p.z[row.down];
        p.z[i] = value * row.inverse_factor;
    }
}
} // namespace
void solve_compact_pressure(State& s) {
    auto& l = s.liquid;
    auto& p = l.compact;
    const int count = static_cast<int>(l.wet.size());
    std::ranges::fill(p.index, 0);
    p.rows.resize(count + 1);
    p.rows[0] = {};
    for (auto* a : {&p.x, &p.b, &p.r, &p.z, &p.direction, &p.product}) {
        a->resize(count + 1);
        (*a)[0] = 0;
    }
    for (int n = 1; n <= count; ++n) p.index[l.wet[n - 1]] = n;
    // Compact the five-point stencil once per projection. Iterations then touch
    // only contiguous wet-cell vectors, without grid-boundary branches or scans
    // across a 61,440-cell allocation for each scalar.
    for (int n = 1; n <= count; ++n) {
        const int i = l.wet[n - 1], x = i % grid_width;
        auto& row = p.rows[n];
        row = {};
        row.diagonal = l.diagonal[i];
        p.x[n] = l.pressure[i];
        p.b[n] = l.rhs[i];
        auto edge = [&](int neighbor, double coefficient, int& index, double& a) {
            if (l.mask[neighbor] && l.anchor[neighbor] != 2) {
                index = p.index[neighbor];
                a = coefficient;
            }
        };
        if (l.anchor[i] != 2) {
            if (x > 0) edge(i - 1, l.right[i - 1], row.left, row.a_left);
            if (x + 1 < grid_width) edge(i + 1, l.right[i], row.right, row.a_right);
            if (i >= grid_width) edge(i - grid_width, l.down[i - grid_width], row.up, row.a_up);
            if (i + grid_width < grid_cells) edge(i + grid_width, l.down[i], row.down, row.a_down);
        }
        double pivot = row.diagonal;
        if (s.config.modified_cholesky) {
            // MIC(0), tau=.97: retain dropped cross terms on the diagonal.
            // This improves low-frequency error removal without changing A/b.
            const auto& lrow = p.rows[row.left];
            const auto& urow = p.rows[row.up];
            pivot -= row.a_left * row.a_left * lrow.inverse_factor + row.a_up * row.a_up * urow.inverse_factor;
            pivot -= .97
                   * (row.a_left * lrow.a_down * lrow.inverse_factor + row.a_up * urow.a_right * urow.inverse_factor);
            if (pivot < .25 * row.diagonal) pivot = row.diagonal;
            row.inverse_factor = 1 / pivot;
        } else {
            const double left = row.a_left * p.rows[row.left].inverse_factor;
            const double up = row.a_up * p.rows[row.up].inverse_factor;
            pivot -= left * left;
            pivot -= up * up;
            row.inverse_factor = 1 / std::sqrt(std::max(row.diagonal * .01, pivot));
        }
    }
    p.coarse.ready = false;
    if (s.config.coarse_pressure && s.config.modified_cholesky && count >= 256) build_coarse(p, l, count);
    multiply(p, p.x, p.product, count);
    for (int n = 1; n <= count; ++n) p.r[n] = p.b[n] - p.product[n];
    precondition(p, count, s.config.modified_cholesky);
    for (int n = 1; n <= count; ++n) p.direction[n] = p.z[n];
    double rz = dot(p.r, p.z, count);
    const double norm_b = std::sqrt(dot(p.b, p.b, count));
    const double tolerance = s.config.pressure_tolerance * std::max(1.0, norm_b);
    double residual = std::sqrt(dot(p.r, p.r, count));
    int iteration = 0;
    for (; iteration < s.config.pressure_iterations && residual > tolerance; ++iteration) {
        multiply(p, p.direction, p.product, count);
        const double denominator = dot(p.direction, p.product, count);
        if (!(denominator > 1e-30)) break;
        const double alpha = rz / denominator;
        for (int n = 1; n <= count; ++n) {
            p.x[n] += alpha * p.direction[n];
            p.r[n] -= alpha * p.product[n];
        }
        residual = std::sqrt(dot(p.r, p.r, count));
        if (residual <= tolerance) {
            ++iteration;
            break;
        }
        precondition(p, count, s.config.modified_cholesky);
        const double next_rz = dot(p.r, p.z, count), beta = rz > 1e-30 ? next_rz / rz : 0;
        for (int n = 1; n <= count; ++n) p.direction[n] = p.z[n] + beta * p.direction[n];
        rz = next_rz;
    }
    // Report the true residual, not only the recursively updated CG residual.
    multiply(p, p.x, p.product, count);
    for (int n = 1; n <= count; ++n) p.r[n] = p.b[n] - p.product[n];
    residual = std::sqrt(dot(p.r, p.r, count));
    for (int n = 1; n <= count; ++n) l.pressure[l.wet[n - 1]] = p.x[n];
    s.report.pressure_iterations += iteration;
    s.report.pressure_residual = std::max(s.report.pressure_residual, residual / std::max(1.0, norm_b));
    s.report.pressure_converged = s.report.pressure_converged && residual <= tolerance;
}
} // namespace strefa::sim::detail
