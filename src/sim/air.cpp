#include "sim/physics.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace strefa::sim::detail {
namespace {
bool open(const State& s, int i) { return s.air.open[i] != 0; }
void cache_air(State& s) {
    s.executor.run(grid_cells,4096,[&](int begin,int end) {
    for (int i = begin; i < end; ++i) {
        double capacity = 0, offset = 0;
        for (unsigned c = 0; c < gas_count; ++c) {
            capacity += s.air.mass[c][i] * gas_cp(static_cast<Gas>(c));
            offset += s.air.mass[c][i] * gas_offset(static_cast<Gas>(c));
        }
        s.air.capacity[i] = capacity;
        s.air.temperature[i] = capacity > 1e-12 ? std::max(1.0, (s.air.energy[i] - offset) / capacity) : ambient_temperature;
    }
    });
}
void transport_gas(State& s, double dt) {
    auto& a = s.air;
    // Only transport near a perturbation. The acoustic stencil still visits the
    // full grid. Untouched ambient cells remain bitwise uniform; the padded box
    // contains every face that can carry a nonzero scalar/heat difference.
    int min_x=grid_width,max_x=-1,min_y=grid_height,max_y=-1;
    for(int i=0;i<grid_cells;++i) {
        if(!open(s,i)) continue;
        const int x=i%grid_width,y=i/grid_width;
        bool disturbed=std::abs(a.temperature[i]-ambient_temperature)>1e-7 || std::abs(gas_mass(s,i)-.02)>1e-12;
        for(unsigned c=gi(Gas::steam);c<gas_count;++c) disturbed=disturbed||a.mass[c][i]>0;
        disturbed=disturbed || std::abs(a.mass[gi(Gas::oxygen)][i]*3-a.mass[gi(Gas::inert)][i])>1e-12
            || std::abs(a.u[u_index(x,y)])>1e-10 || std::abs(a.u[u_index(x+1,y)])>1e-10
            || std::abs(a.v[v_index(x,y)])>1e-10 || std::abs(a.v[v_index(x,y+1)])>1e-10;
        if(disturbed) {min_x=std::min(min_x,x);max_x=std::max(max_x,x);min_y=std::min(min_y,y);max_y=std::max(max_y,y);}
    }
    if(max_x<0) return;
    min_x=std::max(0,min_x-2);max_x=std::min(grid_width-1,max_x+2);
    min_y=std::max(0,min_y-2);max_y=std::min(grid_height-1,max_y+2);
    auto& flows=a.flows;flows.clear();
    std::ranges::fill(a.outflow, 0.0);
    auto face = [&](int i, int j, double velocity) {
        if (!open(s, i) || !open(s, j) || std::abs(velocity) < 1e-10) return;
        const int from = velocity > 0 ? i : j, to = velocity > 0 ? j : i;
        const double fraction = std::abs(velocity) * dt;
        flows.push_back({from, to, fraction}); a.outflow[from] += fraction;
    };
    for (int y = min_y; y <= max_y; ++y) for (int x = min_x; x <= max_x; ++x) {
        const int i = y * grid_width + x;
        if (x + 1 < grid_width) face(i, i + 1, a.u[u_index(x + 1, y)]);
        if (y + 1 < grid_height) face(i, i + grid_width, a.v[v_index(x, y + 1)]);
    }
    for (auto& flow : flows) flow.fraction /= std::max(1.0, a.outflow[flow.from]);
    std::array<bool,gas_count+1> present{};
    for(unsigned c=0;c<gas_count;++c)
        present[c]=std::ranges::any_of(a.mass[c],[](double mass){return mass!=0;});
    present[gas_count]=true;
    // Face lists are immutable during channel jobs. Each job writes its own
    // scalar and scratch; no floating-point atomics or racy scatter updates.
    const int end_x=std::min(grid_width-1,max_x+1),end_y=std::min(grid_height-1,max_y+1);
    auto region=[&](auto&& function) {
        for(int y=min_y;y<=end_y;++y) for(int x=min_x;x<=end_x;++x) function(y*grid_width+x);
    };
    s.executor.run(gas_count+1,1,[&](int begin,int end) {
    for(int channel=begin;channel<end;++channel) {
        if(!present[channel]) continue;
        auto* scalar=channel==gas_count?&a.energy:&a.mass[channel];
        auto& delta=a.delta[channel];
        region([&](int i){delta[i]=0;});
        for (const auto& flow : flows) {
            const double value = (*scalar)[flow.from] * flow.fraction;
            delta[flow.from] -= value; delta[flow.to] += value;
        }
        region([&](int i){(*scalar)[i]+=delta[i];});
    }
    });
    cache_air(s);
    // Oxygen/species diffusion is pairwise conservative. Each donor contributes at
    // most 4*k*dt of its quantity, and k*dt is bounded below 1/4.
    const double diffusivity = std::min(.05 * dt, .24);
    s.executor.run(gas_count,1,[&](int begin,int end) {
    for (unsigned c = static_cast<unsigned>(begin); c < static_cast<unsigned>(end); ++c) {
        if(!present[c]) continue;
        auto& delta=a.delta[c];auto& energy_delta=a.diffusion_energy[c];
        region([&](int i){delta[i]=energy_delta[i]=0;});
        auto diffuse = [&](int i, int j) {
            if (!open(s, i) || !open(s, j)) return;
            const double mass = (a.mass[c][i] - a.mass[c][j]) * diffusivity;
            if (mass == 0) return;
            const int donor = mass > 0 ? i : j;
            const double energy = mass * (gas_cp(static_cast<Gas>(c)) * a.temperature[donor] + gas_offset(static_cast<Gas>(c)));
            delta[i] -= mass; delta[j] += mass;
            energy_delta[i] -= energy; energy_delta[j] += energy;
        };
        for (int y = min_y; y <= max_y; ++y) for (int x = min_x; x <= max_x; ++x) {
            const int i = y * grid_width + x;
            if (x + 1 < grid_width) diffuse(i, i + 1);
            if (y + 1 < grid_height) diffuse(i, i + grid_width);
        }
        region([&](int i){a.mass[c][i]+=delta[i];});
    }
    });
    // Fixed channel order gives the same energy sum for every worker count.
    for(unsigned c=0;c<gas_count;++c) if(present[c]) region([&](int i){a.energy[i]+=a.diffusion_energy[c][i];});
    if (s.config.open_top) for (int x = 0; x < grid_width; ++x) {
        if (!open(s, x)) continue;
        const double speed = a.v[v_index(x, 0)];
        if (speed < 0) {
            const double fraction = std::min(.8, -speed * dt);
            for (auto& mass : a.mass) { const double escaped = mass[x] * fraction; mass[x] -= escaped; s.budget.escaped_mass += escaped; }
            const double escaped = a.energy[x] * fraction; a.energy[x] -= escaped; s.budget.escaped_energy += escaped;
        } else {
            const double amount = .02 * speed * dt;
            a.mass[gi(Gas::inert)][x] += amount * .75; a.mass[gi(Gas::oxygen)][x] += amount * .25;
            a.energy[x] += amount * ambient_temperature;
            s.budget.incoming_mass += amount; s.budget.incoming_energy += amount * ambient_temperature;
        }
    }
}
void diffuse_heat(State& s, double dt) {
    auto& a = s.air;
    std::ranges::fill(a.scalar_delta, 0.0);
    auto face = [&](int i, int j) {
        if (!open(s, i) || !open(s, j) || a.capacity[i] <= 0 || a.capacity[j] <= 0) return;
        const double difference = a.temperature[j] - a.temperature[i];
        const double limit = .24 * std::min(a.capacity[i], a.capacity[j]) * std::abs(difference);
        const double heat = std::clamp(.08 * difference * dt, -limit, limit);
        a.scalar_delta[i] += heat; a.scalar_delta[j] -= heat;
    };
    for (int y = 0; y < grid_height; ++y) for (int x = 0; x < grid_width; ++x) {
        const int i = y * grid_width + x;
        if (x + 1 < grid_width) face(i, i + 1);
        if (y + 1 < grid_height) face(i, i + grid_width);
    }
    for (int i = 0; i < grid_cells; ++i) a.energy[i] += a.scalar_delta[i];
}
}
void solve_air(State& s, double duration) {
    const auto start=std::chrono::steady_clock::now();
    auto& a = s.air;
    for (int i = 0; i < grid_cells; ++i) a.open[i] = static_cast<unsigned char>(!barrier(s, i) && liquid_fill(s, i) < .98);
    cache_air(s);
    s.executor.run(grid_cells,4096,[&](int begin,int end) {
    for (int i=begin;i<end;++i) {
        const double composition=a.mass[gi(Gas::fuel)][i]*.5-a.mass[gi(Gas::steam)][i]*.3;
        a.buoyancy[i]=static_cast<float>(-10*std::clamp((a.temperature[i]-ambient_temperature)/ambient_temperature,-1.0,4.0)
            +8*composition/std::max(.001,gas_mass(s,i)));
    }
    });
    // Staggered pressure/velocity acoustics: c*dt*sqrt(1/dx²+1/dy²) <= 1.
    // Scalar advection has its own CFL below; adding its speed here was redundant.
    const int steps = std::max(1,static_cast<int>(std::ceil(duration * std::sqrt(2.0) * s.config.sound_speed / s.config.cfl)));
    s.report.air_substeps = steps;
    const double dt = duration / steps, bulk = s.config.air_density * s.config.sound_speed * s.config.sound_speed;
    for (int step = 0; step < steps; ++step) {
        for(int y=0;y<grid_height;++y) a.next_u[u_index(0,y)]=a.next_u[u_index(grid_width,y)]=0;
        const double damping = std::exp(-s.config.velocity_damping * dt);
        s.executor.run(grid_height,16,[&](int begin,int end) {
        for (int y = begin; y < end; ++y) for (int x = 1; x < grid_width; ++x) {
            const int left = y * grid_width + x - 1, right = left + 1, face = u_index(x, y);
            if (!open(s, left) || !open(s, right)) { a.next_u[face] = 0; continue; }
            const double gradient = a.pressure[right] - a.pressure[left];
            a.next_u[face] = static_cast<float>(std::clamp((a.u[face] - dt * gradient / s.config.air_density) * damping, -40.0, 40.0));
        }
        for (int y = begin; y < end; ++y) for (int x = 0; x < grid_width; ++x) {
            const int face = v_index(x, y);
            if (y == grid_height || (y == 0 && !s.config.open_top)) { a.next_v[face] = 0; continue; }
            const int bottom = y * grid_width + x, top = y == 0 ? bottom : bottom - grid_width;
            if (!open(s, top) || !open(s, bottom)) { a.next_v[face] = 0; continue; }
            const double gradient = a.pressure[bottom] - (y == 0 ? 0 : a.pressure[top]);
            const double buoyancy = .5 * (a.buoyancy[top]+a.buoyancy[bottom]);
            a.next_v[face] = static_cast<float>(std::clamp((a.v[face] - dt * gradient / s.config.air_density + dt * buoyancy) * damping, -40.0, 40.0));
        }
        });
        for(int x=0;x<grid_width;++x) a.next_v[v_index(x,grid_height)]=0;
        a.u.swap(a.next_u); a.v.swap(a.next_v);
        const double pressure_damping = std::exp(-s.config.pressure_damping * dt);
        s.executor.run(grid_height,16,[&](int begin,int end) {
        for (int y = begin; y < end; ++y) for (int x = 0; x < grid_width; ++x) {
            const int i = y * grid_width + x;
            if (!open(s, i)) { a.next_pressure[i] = 0; continue; }
            const double divergence = a.u[u_index(x + 1, y)] - a.u[u_index(x, y)]
                + a.v[v_index(x, y + 1)] - a.v[v_index(x, y)];
            // Source amplitude is bounded, but its undispatched integral remains
            // queued. No excess source is discarded when an explosion is strong.
            const double source = std::clamp(a.pressure_source[i], -2160*dt, 2160*dt);
            a.pressure_source[i] -= source;
            double pressure = (a.pressure[i] - bulk * dt * divergence + source) * pressure_damping;
            if (s.config.open_top && y < 3) pressure *= std::exp(-6 * dt);
            a.next_pressure[i] = static_cast<float>(pressure);
        }
        });
        a.pressure.swap(a.next_pressure);
    }
    const auto waves=std::chrono::steady_clock::now();
    s.report.air_wave_ms=std::chrono::duration<double,std::milli>(waves-start).count();
    double maximum = 0;
    for (const auto value : a.u) maximum = std::max(maximum, std::abs(static_cast<double>(value)));
    for (const auto value : a.v) maximum = std::max(maximum, std::abs(static_cast<double>(value)));
    const int advection_steps = std::max(1, static_cast<int>(std::ceil(2 * maximum * duration / s.config.cfl)));
    for (int n = 0; n < advection_steps; ++n) {
        transport_gas(s, duration / advection_steps);
        cache_air(s);
    }
    if (s.config.thermal) diffuse_heat(s, duration);
    cache_air(s);
    s.report.air_transport_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waves).count();
}
} // namespace strefa::sim::detail
