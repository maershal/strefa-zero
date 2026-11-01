#include "sim/physics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace strefa::sim::detail {
namespace {
bool move(State& s, int& from, Point target) {
    if (!contains(target)) return false;
    const int to = cell_index(target);
    if (barrier(s, to)) return false;
    const double liquid = liquid_fill(s, to);
    double liquid_density = 0;
    for (std::size_t c = 0; c < liquid_materials.size(); ++c)
        liquid_density += s.liquid.volume[c][to] * properties(liquid_materials[c]).density;
    if (liquid > .05 && properties(s.material[from]).density <= liquid_density / liquid) return false;
    std::swap(s.material[from], s.material[to]);
    std::swap(s.shade[from], s.shade[to]);
    std::swap(s.loose[from], s.loose[to]);
    std::swap(s.burning[from], s.burning[to]);
    for (auto* a : {&s.mass, &s.energy, &s.fuel}) std::swap((*a)[from], (*a)[to]);
    for (auto* a : {&s.vx, &s.vy, &s.offset_x, &s.offset_y}) std::swap((*a)[from], (*a)[to]);
    if (liquid > 0) {
        for (auto& a : s.liquid.volume) std::swap(a[from], a[to]);
        for (auto& a : s.liquid.energy) std::swap(a[from], a[to]);
        std::swap(s.acid_capacity[from], s.acid_capacity[to]);
        std::swap(s.dissolved[from], s.dissolved[to]);
        s.vx[to] *= .8F;
        s.vy[to] *= .8F;
    }
    s.updated[from] = s.updated[to] = s.report.tick;
    wake(s, from);
    wake(s, to);
    ++s.report.moves;
    from = to;
    return true;
}
} // namespace
void move_powders(State& s, double dt, bool full_scan) {
    const bool reverse = (s.report.tick & 1U) != 0;
    for (int y = grid_height - 1; y >= 0; --y)
        for (int n = 0; n < grid_width; ++n) {
            const int x = reverse ? grid_width - 1 - n : n;
            const int chunk = (y / 16) * 20 + x / 16;
            if (!full_scan && !s.movement_awake[chunk]) {
                n += 15;
                continue;
            }
            int i = y * grid_width + x;
            ++s.report.visited_cells;
            if (s.updated[i] == s.report.tick || s.mass[i] <= 0
                || (definition(s.material[i]).phase != Phase::powder && !s.loose[i]))
                continue;
            s.updated[i] = s.report.tick;
            const float wind_x = (s.air.u[u_index(x, y)] + s.air.u[u_index(x + 1, y)]) * .5F;
            const float wind_y = (s.air.v[v_index(x, y)] + s.air.v[v_index(x, y + 1)]) * .5F;
            auto blocked = [&](Point p) { return !contains(p) || barrier(s, cell_index(p)); };
            if (blocked({x, y + 1}) && (blocked({x - 1, y}) || blocked({x - 1, y + 1}))
                && (blocked({x + 1, y}) || blocked({x + 1, y + 1})) && std::abs(s.vx[i]) < .2F && std::abs(s.vy[i]) < 4
                && std::abs(wind_x) < 1 && wind_y > -1) {
                s.vx[i] = s.vy[i] = s.offset_x[i] = s.offset_y[i] = 0;
                continue;
            }
            const double drag = .3 / std::max(.2, properties(s.material[i]).density);
            s.vx[i] = static_cast<float>(std::clamp(s.vx[i] + (wind_x - s.vx[i]) * drag * dt, -120.0, 120.0));
            s.vy[i] = static_cast<float>(
                    std::clamp(s.vy[i] + s.config.gravity * dt + (wind_y - s.vy[i]) * drag * dt, -120.0, 140.0));
            s.offset_x[i] += static_cast<float>(s.vx[i] * dt);
            s.offset_y[i] += static_cast<float>(s.vy[i] * dt);
            for (int sweep = 0; sweep < 8; ++sweep) {
                bool progress = false;
                if (std::abs(s.offset_x[i]) >= 1) {
                    const int direction = s.offset_x[i] > 0 ? 1 : -1;
                    if (move(s, i, {i % grid_width + direction, i / grid_width})) {
                        s.offset_x[i] -= direction;
                        progress = true;
                    } else {
                        s.vx[i] *= -.08F;
                        s.offset_x[i] = 0;
                    }
                }
                if (std::abs(s.offset_y[i]) >= 1) {
                    const int direction = s.offset_y[i] > 0 ? 1 : -1;
                    if (move(s, i, {i % grid_width, i / grid_width + direction})) {
                        s.offset_y[i] -= direction;
                        progress = true;
                    } else {
                        const int side = (random(s, i, 2) & 1U) ? -1 : 1;
                        bool slipped = false;
                        if (direction > 0)
                            for (const int dx : {side, -side}) {
                                const Point lateral{i % grid_width + dx, i / grid_width};
                                if (contains(lateral) && !barrier(s, cell_index(lateral))
                                    && move(s, i, {lateral.x, lateral.y + 1})) {
                                    s.offset_y[i] -= 1;
                                    slipped = progress = true;
                                    break;
                                }
                            }
                        if (!slipped) {
                            s.vy[i] *= -.04F;
                            s.vx[i] *= .6F;
                            s.offset_y[i] = 0;
                        }
                    }
                }
                if (!progress) break;
            }
            if (std::abs(s.vx[i]) > .05F || std::abs(s.vy[i]) > .15F || std::abs(s.offset_y[i]) > .01F) wake(s, i);
        }
}
} // namespace strefa::sim::detail
