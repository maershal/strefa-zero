#include "sim/physics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace strefa::sim::detail {
namespace {
constexpr std::array<int, 4> offsets{-1, 1, -grid_width, grid_width};
bool neighbor(int i, int d, int& j) {
    j = i + d;
    return j >= 0 && j < grid_cells && (d != 1 || i % grid_width != grid_width - 1) && (d != -1 || i % grid_width != 0);
}
double coefficient(const State& s, int a, int b) {
    const auto& l = s.liquid;
    if (barrier(s, a) || barrier(s, b) || (!l.mask[a] && !l.mask[b])) return 0;
    const double density = l.mask[a] && l.mask[b] ? (l.density[a] + l.density[b]) * .5
                         : l.mask[a]              ? l.density[a]
                                                  : l.density[b];
    return 1 / std::max(.1, density);
}
void matrix_product(const LiquidFields& l, const std::vector<double>& x, std::vector<double>& result) {
    for (const int i : l.wet) {
        if (l.anchor[i] == 2) {
            result[i] = x[i];
            continue;
        }
        double value = l.diagonal[i] * x[i];
        const int cx = i % grid_width;
        auto subtract = [&](int j, double a) {
            if (l.mask[j] && l.anchor[j] != 2) value -= a * x[j];
        };
        if (cx > 0) subtract(i - 1, l.right[i - 1]);
        if (cx + 1 < grid_width) subtract(i + 1, l.right[i]);
        if (i >= grid_width) subtract(i - grid_width, l.down[i - grid_width]);
        if (i + grid_width < grid_cells) subtract(i + grid_width, l.down[i]);
        result[i] = value;
    }
}
double dot(const LiquidFields& l, const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0;
    for (const int i : l.wet) sum += a[i] * b[i];
    return sum;
}
void factor(LiquidFields& l) {
    // Incomplete Cholesky with the same five-point sparsity. The operator
    // remains matrix-free; only one inverse diagonal factor is stored per cell.
    for (const int i : l.wet) {
        double pivot = l.diagonal[i];
        if (l.anchor[i] != 2) {
            if (i % grid_width > 0 && l.mask[i - 1] && l.anchor[i - 1] != 2) {
                const double a = l.right[i - 1] * l.inverse_factor[i - 1];
                pivot -= a * a;
            }
            if (i >= grid_width && l.mask[i - grid_width] && l.anchor[i - grid_width] != 2) {
                const double a = l.down[i - grid_width] * l.inverse_factor[i - grid_width];
                pivot -= a * a;
            }
        }
        l.inverse_factor[i] = 1 / std::sqrt(std::max(l.diagonal[i] * .01, pivot));
    }
}
void precondition(LiquidFields& l) {
    for (const int i : l.wet) {
        double value = l.r[i];
        if (l.anchor[i] != 2) {
            if (i % grid_width > 0 && l.mask[i - 1] && l.anchor[i - 1] != 2)
                value += l.right[i - 1] * l.inverse_factor[i - 1] * l.z[i - 1];
            if (i >= grid_width && l.mask[i - grid_width] && l.anchor[i - grid_width] != 2)
                value += l.down[i - grid_width] * l.inverse_factor[i - grid_width] * l.z[i - grid_width];
        }
        l.z[i] = value * l.inverse_factor[i];
    }
    for (auto it = l.wet.rbegin(); it != l.wet.rend(); ++it) {
        const int i = *it;
        double value = l.z[i];
        if (l.anchor[i] != 2) {
            if (i % grid_width + 1 < grid_width && l.mask[i + 1] && l.anchor[i + 1] != 2)
                value += l.right[i] * l.inverse_factor[i] * l.z[i + 1];
            if (i + grid_width < grid_cells && l.mask[i + grid_width] && l.anchor[i + grid_width] != 2)
                value += l.down[i] * l.inverse_factor[i] * l.z[i + grid_width];
        }
        l.z[i] = value * l.inverse_factor[i];
    }
}
void project(State& s, double dt) {
    auto& l = s.liquid;
    std::ranges::fill(l.right, 0.0);
    std::ranges::fill(l.down, 0.0);
    std::ranges::fill(l.anchor, static_cast<unsigned char>(0));
    for (int y = l.min_y; y <= l.max_y; ++y)
        for (int x = l.min_x; x <= l.max_x; ++x) {
            const int i = y * grid_width + x;
            if (x + 1 < grid_width) l.right[i] = coefficient(s, i, i + 1);
            if (y + 1 < grid_height) l.down[i] = coefficient(s, i, i + grid_width);
        }
    // Each completely enclosed component needs one gauge. Neighbor rows retain
    // the diagonal contribution but omit the fixed-zero anchor, preserving SPD.
    auto& component = l.component;
    l.closed_cells.clear();
    l.closed_ends.clear();
    for (const int first : l.wet) {
        if (l.anchor[first]) continue;
        component.clear();
        component.push_back(first);
        l.anchor[first] = 1;
        bool free_surface = false;
        for (std::size_t head = 0; head < component.size(); ++head) {
            const int i = component[head];
            for (const int d : offsets) {
                int j{};
                if (!neighbor(i, d, j) || barrier(s, j)) continue;
                if (!l.mask[j]) {
                    free_surface = true;
                    continue;
                }
                if (!l.anchor[j]) {
                    l.anchor[j] = 1;
                    component.push_back(j);
                }
            }
        }
        if (!free_surface) {
            l.anchor[first] = 2;
            l.closed_cells.insert(l.closed_cells.end(), component.begin(), component.end());
            l.closed_ends.push_back(static_cast<int>(l.closed_cells.size()));
        }
    }
    for (const int i : l.wet) {
        const int x = i % grid_width, y = i / grid_width;
        double diagonal = 0, boundary = 0;
        auto face = [&](int j, double a) {
            diagonal += a;
            if (!l.mask[j]) boundary += a * s.air.pressure[j];
        };
        if (x > 0) face(i - 1, l.right[i - 1]);
        if (x + 1 < grid_width) face(i + 1, l.right[i]);
        if (y > 0) face(i - grid_width, l.down[i - grid_width]);
        if (y + 1 < grid_height) face(i + grid_width, l.down[i]);
        const double divergence =
                l.u[u_index(x + 1, y)] - l.u[u_index(x, y)] + l.v[v_index(x, y + 1)] - l.v[v_index(x, y)];
        l.diagonal[i] = std::max(1e-12, diagonal);
        l.rhs[i] = -divergence / dt + boundary + std::max(0.0, l.fill[i] - l.capacity[i]) * .5 / (dt * dt);
        if (diagonal < 1e-12) {
            l.diagonal[i] = 1;
            l.rhs[i] = l.pressure[i] = 0;
        }
    }
    // A sealed Neumann component cannot create net volume. Remove its
    // incompatible mean BEFORE fixing the pressure gauge; otherwise the anchor
    // silently acts as an artificial volume source/sink.
    int begin = 0;
    for (const int end : l.closed_ends) {
        double mean = 0;
        for (int n = begin; n < end; ++n) mean += l.rhs[l.closed_cells[n]];
        mean /= end - begin;
        for (int n = begin; n < end; ++n) l.rhs[l.closed_cells[n]] -= mean;
        const int anchor = l.closed_cells[begin];
        l.diagonal[anchor] = 1;
        l.rhs[anchor] = l.pressure[anchor] = 0;
        begin = end;
    }
    if (s.config.reference_pressure) {
        for (auto* a : {&l.r, &l.z, &l.direction, &l.product, &l.inverse_factor})
            if (a->empty()) a->resize(grid_cells);
        matrix_product(l, l.pressure, l.product);
        for (const int i : l.wet) {
            l.r[i] = l.rhs[i] - l.product[i];
        }
        factor(l);
        precondition(l);
        for (const int i : l.wet) l.direction[i] = l.z[i];
        double rz = dot(l, l.r, l.z);
        const double norm_b = std::sqrt(dot(l, l.rhs, l.rhs));
        const double tolerance = s.config.pressure_tolerance * std::max(1.0, norm_b);
        double residual = std::sqrt(dot(l, l.r, l.r));
        int iteration = 0;
        for (; iteration < s.config.pressure_iterations && residual > tolerance; ++iteration) {
            matrix_product(l, l.direction, l.product);
            const double denominator = dot(l, l.direction, l.product);
            if (!(denominator > 1e-30)) break;
            const double alpha = rz / denominator;
            for (const int i : l.wet) {
                l.pressure[i] += alpha * l.direction[i];
                l.r[i] -= alpha * l.product[i];
            }
            precondition(l);
            residual = std::sqrt(dot(l, l.r, l.r));
            const double next_rz = dot(l, l.r, l.z), beta = rz > 1e-30 ? next_rz / rz : 0;
            for (const int i : l.wet) l.direction[i] = l.z[i] + beta * l.direction[i];
            rz = next_rz;
        }
        s.report.pressure_iterations += iteration;
        s.report.pressure_residual = std::max(s.report.pressure_residual, residual / std::max(1.0, norm_b));
        s.report.pressure_converged = s.report.pressure_converged && residual <= tolerance;
    } else
        solve_compact_pressure(s);
    auto pressure = [&](int i) { return l.mask[i] ? l.pressure[i] : static_cast<double>(s.air.pressure[i]); };
    for (int y = l.min_y; y <= l.max_y; ++y)
        for (int x = std::max(1, l.min_x); x <= std::min(grid_width - 1, l.max_x + 1); ++x) {
            const int i = y * grid_width + x - 1;
            const double a = l.right[i];
            l.u[u_index(x, y)] =
                    a > 0 ? static_cast<float>(l.u[u_index(x, y)] - dt * a * (pressure(i + 1) - pressure(i))) : 0;
        }
    for (int y = std::max(1, l.min_y); y <= std::min(grid_height - 1, l.max_y + 1); ++y)
        for (int x = l.min_x; x <= l.max_x; ++x) {
            const int i = (y - 1) * grid_width + x;
            const double a = l.down[i];
            l.v[v_index(x, y)] =
                    a > 0 ? static_cast<float>(l.v[v_index(x, y)] - dt * a * (pressure(i + grid_width) - pressure(i)))
                          : 0;
        }
    for (const int i : l.wet) {
        const int x = i % grid_width, y = i / grid_width;
        const double divergence =
                l.u[u_index(x + 1, y)] - l.u[u_index(x, y)] + l.v[v_index(x, y + 1)] - l.v[v_index(x, y)];
        s.report.max_divergence = std::max(s.report.max_divergence, std::abs(divergence));
    }
}
void transport(State& s, double dt) {
    auto& l = s.liquid;
    auto& transfers = l.transfers;
    transfers.clear();
    std::ranges::fill(l.outflow, 0.0);
    auto face = [&](int a, int b, double velocity) {
        if (barrier(s, a) || barrier(s, b) || std::abs(velocity) < 1e-10) return;
        const int from = velocity > 0 ? a : b, to = velocity > 0 ? b : a;
        const double swept = std::min(1.0, std::abs(velocity) * dt), fill = std::min(1.0, l.fill[from]);
        // Conservative donor-cell flux in BOTH directions. Assuming every
        // partial cell occupies its bottom interval moved a falling film by a
        // whole cell per substep, independent of u*dt. Geometric VOF requires an
        // actual interface reconstruction; a fill fraction alone is insufficient.
        const double amount = swept * fill;
        if (amount <= 1e-14) return;
        transfers.push_back({from, to, amount});
        l.outflow[from] += amount;
    };
    for (int y = l.min_y; y <= l.max_y; ++y)
        for (int x = l.min_x; x <= l.max_x; ++x) {
            const int i = y * grid_width + x;
            if (x + 1 < grid_width) face(i, i + 1, l.u[u_index(x + 1, y)]);
            if (y + 1 < grid_height) face(i, i + grid_width, l.v[v_index(x, y + 1)]);
        }
    for (auto& transfer : transfers)
        if (l.outflow[transfer.from] > l.fill[transfer.from])
            transfer.volume *= l.fill[transfer.from] / l.outflow[transfer.from];
    const int end_x = std::min(grid_width - 1, l.max_x + 1), end_y = std::min(grid_height - 1, l.max_y + 1);
    auto region = [&](auto&& function) {
        for (int y = l.min_y; y <= end_y; ++y)
            for (int x = l.min_x; x <= end_x; ++x) function(y * grid_width + x);
    };
    for (std::size_t channel = 0; channel < liquid_materials.size(); ++channel) {
        if (!l.present[channel]) continue;
        std::ranges::fill(l.delta_volume, 0.0);
        std::ranges::fill(l.delta_energy, 0.0);
        for (const auto& t : transfers) {
            if (l.fill[t.from] <= 0) continue;
            const double fraction = t.volume / l.fill[t.from];
            const double volume = l.volume[channel][t.from] * fraction, energy = l.energy[channel][t.from] * fraction;
            l.delta_volume[t.from] -= volume;
            l.delta_volume[t.to] += volume;
            l.delta_energy[t.from] -= energy;
            l.delta_energy[t.to] += energy;
        }
        region([&](int i) {
            l.volume[channel][i] += l.delta_volume[i];
            l.energy[channel][i] += l.delta_energy[i];
            if (std::abs(l.delta_volume[i]) > 1e-8) wake(s, i);
        });
    }
    if (!l.solutes) return;
    for (auto* scalar : {&s.acid_capacity, &s.dissolved}) {
        std::ranges::fill(l.delta_volume, 0.0);
        for (const auto& t : transfers) {
            const double amount = (*scalar)[t.from] * (t.volume / l.fill[t.from]);
            l.delta_volume[t.from] -= amount;
            l.delta_volume[t.to] += amount;
        }
        region([&](int i) { (*scalar)[i] += l.delta_volume[i]; });
    }
}
void separate_density(State& s, double dt) {
    auto& l = s.liquid;
    // Conservative sub-cell counterflow seeds unresolved buoyancy at perfectly
    // flat, unstable interfaces. It exchanges equal volumes and their enthalpy.
    if (std::ranges::count(l.present, true) < 2) return;
    const double separation_fraction = -std::expm1(-dt * 1.5);
    for (int y = std::min(grid_height - 2, l.max_y); y >= l.min_y; --y)
        for (int x = l.min_x; x <= l.max_x; ++x) {
            const int a = y * grid_width + x, b = a + grid_width;
            if (barrier(s, a) || barrier(s, b) || l.fill[a] < .8 || l.fill[b] < .8) continue;
            for (std::size_t ca = 0; ca < liquid_materials.size(); ++ca)
                for (std::size_t cb = 0; cb < liquid_materials.size(); ++cb) {
                    if (!l.present[ca] || !l.present[cb]) continue;
                    if (properties(liquid_materials[ca]).density <= properties(liquid_materials[cb]).density + .05)
                        continue;
                    const double available = std::min(l.volume[ca][a], l.volume[cb][b]);
                    const double volume = separation_fraction * available;
                    if (volume < 1e-12) continue;
                    const double ea = l.energy[ca][a] * volume / l.volume[ca][a],
                                 eb = l.energy[cb][b] * volume / l.volume[cb][b];
                    l.volume[ca][a] -= volume;
                    l.volume[ca][b] += volume;
                    l.volume[cb][b] -= volume;
                    l.volume[cb][a] += volume;
                    l.energy[ca][a] -= ea;
                    l.energy[ca][b] += ea;
                    l.energy[cb][b] -= eb;
                    l.energy[cb][a] += eb;
                }
        }
}
void extrapolate_velocity(State& s) {
    auto& l = s.liquid;
    // Advecting liquid enters previously dry cells. A zero velocity just beyond
    // the interface removes momentum on every substep. Extend the projected MAC
    // velocity by two faces (CFL < 1), without crossing a blocked face. This is a
    // velocity boundary extension, not mass/energy transported into empty cells.
    auto extend = [&](std::vector<float>& field, std::vector<float>& next, bool horizontal) {
        const int columns = horizontal ? grid_width + 1 : grid_width;
        const int rows = horizontal ? grid_height : grid_height + 1;
        auto& valid = l.velocity_valid;
        auto& next_valid = l.velocity_next_valid;
        std::ranges::fill(valid, static_cast<unsigned char>(0));
        const int x0 = std::max(1, l.min_x - 2), x1 = std::min(columns - 2, l.max_x + 2);
        const int y0 = std::max(1, l.min_y - 2), y1 = std::min(rows - 2, l.max_y + 2);
        auto cells = [&](int x, int y) {
            const int b = y * grid_width + x;
            return std::pair{b - (horizontal ? 1 : grid_width), b};
        };
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const auto [a, b] = cells(x, y);
                if (!barrier(s, a) && !barrier(s, b) && (l.mask[a] || l.mask[b])) valid[y * columns + x] = 1;
            }
        for (int layer = 0; layer < 2; ++layer) {
            next = field;
            next_valid = valid;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const int i = y * columns + x;
                    if (valid[i]) continue;
                    const auto [a, b] = cells(x, y);
                    if (barrier(s, a) || barrier(s, b)) continue;
                    double sum = 0;
                    int count = 0;
                    for (const int j : {i - 1, i + 1, i - columns, i + columns})
                        if (valid[j]) {
                            sum += field[j];
                            ++count;
                        }
                    if (count) {
                        next[i] = static_cast<float>(sum / count);
                        next_valid[i] = 1;
                    }
                }
            field.swap(next);
            valid.swap(next_valid);
        }
    };
    extend(l.u, l.next_u, true);
    extend(l.v, l.next_v, false);
}
} // namespace
void solve_liquids(State& s, double dt) {
    auto& l = s.liquid;
    const double duration = dt;
    double remaining = duration;
    constexpr std::array<double, 6> viscosities{.06, 1.2, .1, 3, .18, .4};
    double max_nu = 0;
    for (std::size_t c = 0; c < liquid_materials.size(); ++c)
        if (std::ranges::any_of(l.volume[c], [](double volume) { return volume != 0; }))
            max_nu = std::max(max_nu, viscosities[c]);
    s.report.liquid_substeps = 0;
    if (max_nu == 0) {
        std::ranges::fill(l.u, 0.F);
        std::ranges::fill(l.v, 0.F);
        std::ranges::fill(l.pressure, 0.0);
        std::ranges::fill(l.fill, 0.0);
        std::ranges::fill(l.mask, static_cast<unsigned char>(0));
        return;
    }
    while (remaining > duration * 1e-12) {
        double max_u = 0, max_v = 0;
        for (const auto value : l.u) max_u = std::max(max_u, std::abs(static_cast<double>(value)));
        for (const auto value : l.v) max_v = std::max(max_v, std::abs(static_cast<double>(value)));
        // Unsplit upwind convection + explicit 2D viscosity requires the SUM of
        // directional Courant numbers and 4*nu*dt, not max(|u|,|v|)*dt.
        dt = std::min(remaining,
                      s.config.liquid_cfl
                              / std::max(1e-12, max_u + max_v + std::abs(s.config.gravity) * duration + 4 * max_nu));
        remaining -= dt;
        ++s.report.liquid_substeps;
        for (int y = 0; y < grid_height; ++y) l.u[u_index(0, y)] = l.u[u_index(grid_width, y)] = 0;
        for (int x = 0; x < grid_width; ++x) l.v[v_index(x, 0)] = l.v[v_index(x, grid_height)] = 0;
        l.wet.clear();
        l.present.fill(false);
        l.solutes = false;
        l.min_x = grid_width - 1;
        l.max_x = 0;
        l.min_y = grid_height - 1;
        l.max_y = 0;
        struct Row {
            int first{grid_width}, last{-1};
            std::array<bool, liquid_materials.size()> present{};
            bool solutes{};
            double overfill{};
        };
        std::array<Row, grid_height> rows{};
        s.executor.run(grid_height, 16, [&](int begin, int end) {
            for (int y = begin; y < end; ++y) {
                auto& row = rows[y];
                for (int i = y * grid_width; i < (y + 1) * grid_width; ++i) {
                    double mass = 0, fill = 0, nu_sum = 0;
                    for (std::size_t c = 0; c < liquid_materials.size(); ++c) {
                        const double volume = l.volume[c][i];
                        fill += volume;
                        mass += volume * properties(liquid_materials[c]).density;
                        row.present[c] = row.present[c] || volume != 0 || l.energy[c][i] != 0;
                        nu_sum += volume * viscosities[c];
                    }
                    l.viscosity[i] = nu_sum;
                    l.fill[i] = fill;
                    l.capacity[i] = barrier(s, i) ? 0 : 1;
                    l.mask[i] = fill > 1e-5 && l.capacity[i] > 0;
                    row.solutes = row.solutes || s.acid_capacity[i] != 0 || s.dissolved[i] != 0;
                    if (fill > 0) {
                        const int x = i % grid_width;
                        row.first = std::min(row.first, x);
                        row.last = x;
                    }
                    l.density[i] = l.fill[i] > 0 ? mass / l.fill[i] : 1;
                    if (!l.mask[i]) l.pressure[i] = 0;
                    row.overfill = std::max(row.overfill, fill - 1);
                }
            }
        });
        for (int y = 0; y < grid_height; ++y) {
            const auto& row = rows[y];
            for (std::size_t c = 0; c < liquid_materials.size(); ++c) l.present[c] = l.present[c] || row.present[c];
            l.solutes = l.solutes || row.solutes;
            s.report.max_overfill = std::max(s.report.max_overfill, row.overfill);
            if (row.last < 0) continue;
            l.min_x = std::min(l.min_x, row.first);
            l.max_x = std::max(l.max_x, row.last);
            l.min_y = std::min(l.min_y, y);
            l.max_y = std::max(l.max_y, y);
            for (int x = row.first; x <= row.last; ++x)
                if (l.mask[y * grid_width + x]) l.wet.push_back(y * grid_width + x);
        }
        if (l.wet.empty()) return;
        l.min_x = std::max(0, l.min_x - 1);
        l.max_x = std::min(grid_width - 1, l.max_x + 1);
        l.min_y = std::max(0, l.min_y - 1);
        l.max_y = std::min(grid_height - 1, l.max_y + 1);
        // Explicit staggered-grid convection and phase-dependent viscosity.
        // A symmetric four-face Laplacian diffuses momentum; CFL bounds both
        // the upwind advective term and (nu <= 3) the diffusion term.
        auto viscosity = [&](int a, int b) {
            const double fill = l.fill[a] + l.fill[b];
            return fill > 0 ? (l.viscosity[a] + l.viscosity[b]) / fill : .06;
        };
        auto advance = [&](const std::vector<float>& field, int face, int stride, double ux, double vy, double nu) {
            const double center = field[face];
            const double left = field[face - 1], right = field[face + 1];
            const double up = field[face - stride], down = field[face + stride];
            const double dx = ux >= 0 ? center - left : right - center;
            const double dy = vy >= 0 ? center - up : down - center;
            return center + dt * (-ux * dx - vy * dy + nu * (left + right + up + down - 4 * center));
        };
        std::ranges::fill(l.next_u, 0.F);
        std::ranges::fill(l.next_v, 0.F);
        for (int y = l.min_y; y <= l.max_y; ++y)
            for (int x = std::max(1, l.min_x); x <= std::min(grid_width - 1, l.max_x); ++x) {
                const int a = y * grid_width + x - 1, b = a + 1, face = u_index(x, y);
                if (barrier(s, a) || barrier(s, b) || (!l.mask[a] && !l.mask[b]))
                    l.next_u[face] = 0;
                else if (y > 0 && y + 1 < grid_height) {
                    const double vy = .25
                                    * (l.v[v_index(x - 1, y)] + l.v[v_index(x, y)] + l.v[v_index(x - 1, y + 1)]
                                       + l.v[v_index(x, y + 1)]);
                    l.next_u[face] = static_cast<float>(std::clamp(
                            advance(l.u, face, grid_width + 1, l.u[face], vy, viscosity(a, b)), -120.0, 120.0));
                }
            }
        for (int y = std::max(1, l.min_y); y <= std::min(grid_height - 1, l.max_y); ++y)
            for (int x = l.min_x; x <= l.max_x; ++x) {
                const int a = (y - 1) * grid_width + x, b = a + grid_width, face = v_index(x, y);
                if (barrier(s, a) || barrier(s, b) || (!l.mask[a] && !l.mask[b]))
                    l.next_v[face] = 0;
                else if (x > 0 && x + 1 < grid_width) {
                    const double ux = .25
                                    * (l.u[u_index(x, y - 1)] + l.u[u_index(x + 1, y - 1)] + l.u[u_index(x, y)]
                                       + l.u[u_index(x + 1, y)]);
                    l.next_v[face] = static_cast<float>(std::clamp(
                            advance(l.v, face, grid_width, ux, l.v[face], viscosity(a, b)) + s.config.gravity * dt,
                            -120.0, 120.0));
                }
            }
        l.u.swap(l.next_u);
        l.v.swap(l.next_v);
        const auto start = std::chrono::steady_clock::now();
        project(s, dt);
        extrapolate_velocity(s);
        const auto projected = std::chrono::steady_clock::now();
        transport(s, dt);
        separate_density(s, dt);
        s.report.pressure_ms += std::chrono::duration<double, std::milli>(projected - start).count();
        s.report.transport_ms +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - projected).count();
    }
}
} // namespace strefa::sim::detail
