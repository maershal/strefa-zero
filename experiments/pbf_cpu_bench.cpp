// Z01 addendum: CPU microbenchmark of the two PBF passes of liquids.comp (operation 0: density and lambda,
// operation 1: Spiky correction), so "GPU or CPU" has a measured number instead of an estimate.
//
// A measurement tool, not a change to the CPU reference model: ADR 0001 stays in force and nothing in src/sim is
// touched or linked. Built only with -DSTREFA_EXPERIMENT_PBF_CPU=ON, never installed or packaged.
//
// Kept from the GPU path: h = 2, 2D Poly6 density, 2D Spiky pressure gradient, unit-volume solid boundary samples in
// the 5x5 cell window, inverse-mass weighted lambda denominator, the s_corr anti-clustering term, the correction
// clamp, the compensated (residual) move applied twice, Jacobi ping-pong, and neighbours found through cells
// counting-sorted as in neighbors.comp (count, exclusive prefix, scatter; row ranges [start(x0), start(x1 + 1))),
// with the particle data copied into sorted order in SoA arrays (the HOT_READS == 2 layout).
// Left out, all in the CPU's favour: coupling with the soldier body, the visibility test through walls, terrain
// clamping after the move, the reaction channel write, all other passes of the GPU tick (prediction, XSPH, contacts).
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace strefa::experiments {
namespace {

// The Z01 sweep tank (src/gpu/cost_model.cpp, Tank): 300 columns between two walls from x = 10, floor at
// y = height - 5, one particle per cell from the floor up. The world size is derived, not written as a literal.
struct Tank {
    static constexpr int left = 10, columns = 300, floor = 187;
    static constexpr int width = 2 * left + columns, height = floor + 5;
};
constexpr int cell_count = Tank::width * Tank::height;

constexpr float support = 2.0F, reference_density = 1.014612762210833F;
constexpr float s_corr = .0001F;            // FluidOptions::s_corr default
constexpr float water_inverse_mass = 2.65F; // inverseMass(5) in particle_common.glsl
constexpr float pi = std::numbers::pi_v<float>;

float kernel(float dx, float dy) {
    const float q = std::max(0.0F, 4.0F - (dx * dx + dy * dy));
    return q * q * q / (64.0F * pi);
}
struct Vec2 {
    float x{}, y{};
};
Vec2 pressure_gradient(float dx, float dy) {
    const float r = std::sqrt(dx * dx + dy * dy);
    if (!(r > .000001F && r < support)) return {};
    const float s = -30.0F * (2.0F - r) * (2.0F - r) / (32.0F * pi * r);
    return {s * dx, s * dy};
}
const float scorr_reference = kernel(.6F, 0.0F);

// Compensated move of particle_common.glsl move(): position + residual carry the exact sum.
void move(float& position, float& residual, float delta) {
    const float increment = delta + residual;
    const float sum = position + increment;
    residual = increment - (sum - position);
    position = sum;
}

struct Particles {
    std::vector<float> x, y, rx, ry, inverse_mass;
    void resize(std::size_t n) {
        for (auto* v : {&x, &y, &rx, &ry, &inverse_mass}) v->assign(n, 0.0F);
    }
};

// Sense-reversing spin barrier: the cheapest synchronisation between passes (in the CPU's favour; std::barrier wakes
// sleeping threads through the OS).
class SpinBarrier {
public:
    explicit SpinBarrier(int count) : count_{count}, waiting_{count} {}
    void arrive_and_wait() {
        const bool sense = !sense_.load(std::memory_order_relaxed);
        if (waiting_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            waiting_.store(count_, std::memory_order_relaxed);
            sense_.store(sense, std::memory_order_release);
            return;
        }
        for (unsigned spin = 0; sense_.load(std::memory_order_acquire) != sense; ++spin)
            if (spin > 4096) std::this_thread::yield();
    }

private:
    int count_;
    std::atomic<int> waiting_;
    std::atomic<bool> sense_{false};
};

// Persistent workers; the calling thread is worker 0. Static contiguous chunks of the sorted particles.
class Pool {
public:
    explicit Pool(int threads) : threads_{threads}, start_{threads}, done_{threads} {
        for (int k = 1; k < threads; ++k)
            workers_.emplace_back([this, k] {
                for (;;) {
                    start_.arrive_and_wait();
                    if (stop_.load(std::memory_order_relaxed)) return;
                    job_(k);
                    done_.arrive_and_wait();
                }
            });
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() {
        stop_.store(true, std::memory_order_relaxed);
        if (threads_ > 1) start_.arrive_and_wait();
    }
    // fn(begin, end) over [0, n).
    template<class F> void run(std::size_t n, F&& fn) {
        if (threads_ == 1) {
            fn(std::size_t{0}, n);
            return;
        }
        job_ = [&](int k) {
            const auto t = static_cast<std::size_t>(threads_), i = static_cast<std::size_t>(k);
            fn(n * i / t, n * (i + 1) / t);
        };
        start_.arrive_and_wait();
        job_(0);
        done_.arrive_and_wait();
    }
    int threads() const { return threads_; }

private:
    int threads_;
    SpinBarrier start_, done_;
    std::function<void(int)> job_;
    std::atomic<bool> stop_{false};
    std::vector<std::jthread> workers_;
};

class Solver {
public:
    explicit Solver(std::size_t n) : n_{n} {
        solid_.assign(cell_count, 0);
        for (int y = 0; y < Tank::height; ++y)
            for (int x = 0; x < Tank::width; ++x)
                if (y >= Tank::floor || x == Tank::left - 1 || x == Tank::left + Tank::columns || x == 0
                    || x == Tank::width - 1)
                    solid_[static_cast<std::size_t>(y * Tank::width + x)] = 1;
        start_.assign(cell_count + 1, 0);
        count_.assign(cell_count, 0);
        cell_of_.assign(n, 0);
        lambda_.assign(n, 0.0F);
        density_.assign(n, 0.0F);
        current_.resize(n);
        next_.resize(n);
        initial_.resize(n);
        // Same placement as the Z01 tank plus a small deterministic jitter (SplitMix64), so the constraint is
        // active; the work per pass does not depend on it.
        std::uint64_t state = 0x5A01;
        auto jitter = [&] {
            state += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z ^= z >> 31;
            return (static_cast<float>(z >> 40) / static_cast<float>(1U << 24) - .5F) * .2F;
        };
        for (std::size_t i = 0; i < n; ++i) {
            initial_.x[i] = static_cast<float>(Tank::left) + .5F + static_cast<float>(i % Tank::columns) + jitter();
            initial_.y[i] = static_cast<float>(Tank::floor) - .5F - static_cast<float>(i / Tank::columns) + jitter();
            initial_.inverse_mass[i] = water_inverse_mass;
        }
    }

    void reset() { current_ = initial_; }

    // neighbors.comp operations 0-4: clear, count, exclusive prefix, scatter into sorted order; also the largest
    // residual (cells[0]), which widens the search window. Single-threaded.
    void sort() {
        std::ranges::fill(count_, 0U);
        float largest = 0.0F;
        for (std::size_t i = 0; i < n_; ++i) {
            const int cx = std::clamp(static_cast<int>(std::floor(current_.x[i])), 0, Tank::width - 1);
            const int cy = std::clamp(static_cast<int>(std::floor(current_.y[i])), 0, Tank::height - 1);
            const auto c = static_cast<std::uint32_t>(cy * Tank::width + cx);
            cell_of_[i] = c;
            ++count_[c];
            largest = std::max(largest, std::sqrt(current_.rx[i] * current_.rx[i] + current_.ry[i] * current_.ry[i]));
        }
        std::uint32_t sum = 0;
        for (std::size_t c = 0; c < static_cast<std::size_t>(cell_count); ++c) {
            start_[c] = sum;
            sum += count_[c];
        }
        start_[cell_count] = sum;
        std::ranges::fill(count_, 0U);
        for (std::size_t i = 0; i < n_; ++i) {
            const auto c = cell_of_[i];
            const auto index = start_[c] + count_[c]++;
            next_.x[index] = current_.x[i];
            next_.y[index] = current_.y[i];
            next_.rx[index] = current_.rx[i];
            next_.ry[index] = current_.ry[i];
            next_.inverse_mass[index] = current_.inverse_mass[i];
        }
        std::swap(current_, next_);
        largest_residual_ = largest;
    }

    // liquids.comp operation 0.
    void density_pass(Pool& pool) {
        pool.run(n_, [this](std::size_t begin, std::size_t end) {
            const Particles& p = current_;
            for (std::size_t i = begin; i < end; ++i) {
                const float px = p.x[i], py = p.y[i], prx = p.rx[i], pry = p.ry[i];
                const int cx = static_cast<int>(std::floor(px)), cy = static_cast<int>(std::floor(py));
                float density = kernel(0.0F, 0.0F), denominator = 0.0F, gx = 0.0F, gy = 0.0F;
                for (int y = -2; y <= 2; ++y)
                    for (int x = -2; x <= 2; ++x)
                        if (solid(cx + x, cy + y)) {
                            const float dx = (px - (static_cast<float>(cx + x) + .5F)) + prx;
                            const float dy = (py - (static_cast<float>(cy + y) + .5F)) + pry;
                            density += kernel(dx, dy);
                            const Vec2 g = pressure_gradient(dx, dy);
                            gx += g.x / reference_density;
                            gy += g.y / reference_density;
                        }
                for_neighbors(i, [&](std::size_t j) {
                    const float dx = (px - p.x[j]) + (prx - p.rx[j]), dy = (py - p.y[j]) + (pry - p.ry[j]);
                    if (dx * dx + dy * dy >= 4.0F) return;
                    density += kernel(dx, dy);
                    Vec2 g = pressure_gradient(dx, dy);
                    g.x /= reference_density;
                    g.y /= reference_density;
                    gx += g.x;
                    gy += g.y;
                    denominator += p.inverse_mass[j] * (g.x * g.x + g.y * g.y);
                });
                const float compression = std::max(0.0F, density / reference_density - 1.0F);
                lambda_[i] = -compression / (denominator + p.inverse_mass[i] * (gx * gx + gy * gy) + .0001F);
                density_[i] = density / reference_density;
            }
        });
    }

    // liquids.comp operation 1 (liquid carriers only; every particle here is water).
    void correction_pass(Pool& pool) {
        pool.run(n_, [this](std::size_t begin, std::size_t end) {
            const Particles& p = current_;
            Particles& out = next_;
            for (std::size_t i = begin; i < end; ++i) {
                float px = p.x[i], py = p.y[i], prx = p.rx[i], pry = p.ry[i];
                const int cx = static_cast<int>(std::floor(px)), cy = static_cast<int>(std::floor(py));
                const float own = lambda_[i];
                float corr_x = 0.0F, corr_y = 0.0F;
                for (int y = -2; y <= 2; ++y)
                    for (int x = -2; x <= 2; ++x)
                        if (solid(cx + x, cy + y)) {
                            const Vec2 g = pressure_gradient((px - (static_cast<float>(cx + x) + .5F)) + prx,
                                                             (py - (static_cast<float>(cy + y) + .5F)) + pry);
                            corr_x += own * g.x;
                            corr_y += own * g.y;
                        }
                for_neighbors(i, [&](std::size_t j) {
                    const float dx = (px - p.x[j]) + (prx - p.rx[j]), dy = (py - p.y[j]) + (pry - p.ry[j]);
                    if (dx * dx + dy * dy >= 4.0F) return;
                    const float ratio = kernel(dx, dy) / scorr_reference;
                    const float repel = -s_corr * ratio * ratio * ratio * ratio;
                    const Vec2 g = pressure_gradient(dx, dy);
                    const float w = own + lambda_[j] + repel;
                    corr_x += w * g.x;
                    corr_y += w * g.y;
                });
                const float factor = p.inverse_mass[i] / reference_density;
                corr_x *= factor;
                corr_y *= factor;
                const float length = std::sqrt(corr_x * corr_x + corr_y * corr_y);
                const float scale = std::min(.8F, .2F / std::max(.00001F, length));
                corr_x *= scale;
                corr_y *= scale;
                for (int k = 0; k < 2; ++k) {
                    move(px, prx, corr_x * .5F);
                    move(py, pry, corr_y * .5F);
                }
                out.x[i] = px;
                out.y[i] = py;
                out.rx[i] = prx;
                out.ry[i] = pry;
                out.inverse_mass[i] = p.inverse_mass[i];
            }
        });
        std::swap(current_, next_);
    }

    // Untimed diagnostics: mean neighbours inside h, mean and largest density ratio of the last density pass.
    struct Stats {
        double neighbors{}, mean_density{}, max_density{};
    };
    Stats stats() {
        Stats s;
        for (std::size_t i = 0; i < n_; ++i) {
            for_neighbors(i, [&](std::size_t j) {
                const float dx = current_.x[i] - current_.x[j], dy = current_.y[i] - current_.y[j];
                if (dx * dx + dy * dy < 4.0F) s.neighbors += 1;
            });
            s.mean_density += density_[i];
            s.max_density = std::max(s.max_density, static_cast<double>(density_[i]));
        }
        s.neighbors /= static_cast<double>(n_);
        s.mean_density /= static_cast<double>(n_);
        return s;
    }

    // FNV-1a over the bits of the state; the passes are Jacobi, so any thread count must give the same value.
    std::uint64_t checksum() const {
        std::uint64_t h = 0xCBF29CE484222325ULL;
        for (const auto* v : {&current_.x, &current_.y, &current_.rx, &current_.ry})
            for (const float f : *v) {
                h ^= std::bit_cast<std::uint32_t>(f);
                h *= 0x100000001B3ULL;
            }
        return h;
    }

private:
    bool solid(int x, int y) const {
        return x < 0 || y < 0 || x >= Tank::width || y >= Tank::height
            || solid_[static_cast<std::size_t>(y * Tank::width + x)] != 0;
    }

    // FOR_NEIGHBOR with SORTED_NEIGHBORS: rows of the clamped window, each a contiguous range of sorted entries.
    template<class F> void for_neighbors(std::size_t i, F&& visit) const {
        const float px = current_.x[i], py = current_.y[i];
        const float reach = support + largest_residual_
                          + std::sqrt(current_.rx[i] * current_.rx[i] + current_.ry[i] * current_.ry[i]) + .001F;
        const int x0 = std::clamp(static_cast<int>(std::floor(px - reach)), 0, Tank::width - 1);
        const int x1 = std::clamp(static_cast<int>(std::floor(px + reach)), 0, Tank::width - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(py - reach)), 0, Tank::height - 1);
        const int y1 = std::clamp(static_cast<int>(std::floor(py + reach)), 0, Tank::height - 1);
        for (int row = y0; row <= y1; ++row) {
            const auto end = start_[static_cast<std::size_t>(row * Tank::width + x1 + 1)];
            for (auto entry = start_[static_cast<std::size_t>(row * Tank::width + x0)]; entry < end; ++entry)
                if (entry != i) visit(static_cast<std::size_t>(entry));
        }
    }

    std::size_t n_;
    std::vector<std::uint8_t> solid_;
    std::vector<std::uint32_t> start_, count_, cell_of_;
    std::vector<float> lambda_, density_;
    Particles current_, next_, initial_;
    float largest_residual_{};
};

struct Options {
    // The Z01 GPU sweep points plus 250 and 500, below the smallest GPU point, to bracket the crossover.
    std::vector<std::size_t> counts{250, 500, 1000, 2000, 4000, 8000, 16000, 32000};
    std::vector<int> threads{1, 2, 4};
    double seconds{1.0};
    int blocks{5};
    double turbo_tolerance{1.10};
    int substeps{2}, iterations{8}; // FluidOptions defaults: 2 x 8 = 16 density + 16 correction passes per tick
    std::filesystem::path out{"pbf-cpu-bench"};
};

template<class T> std::vector<T> parse_list(std::string_view text) {
    std::vector<T> values;
    for (const auto part : text | std::views::split(',')) {
        const std::string item{part.begin(), part.end()};
        values.push_back(static_cast<T>(std::stoll(item)));
    }
    return values;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                std::cerr << "Brak wartości dla " << arg << '\n';
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--n")
            o.counts = parse_list<std::size_t>(value());
        else if (arg == "--threads")
            o.threads = parse_list<int>(value());
        else if (arg == "--seconds")
            o.seconds = std::stod(std::string{value()});
        else if (arg == "--blocks")
            o.blocks = std::stoi(std::string{value()});
        else if (arg == "--turbo-tolerance")
            o.turbo_tolerance = std::stod(std::string{value()});
        else if (arg == "--out")
            o.out = std::filesystem::path{std::string{value()}};
        else {
            std::cerr << "Użycie: StrefaPbfCpuBench [--n 1000,2000,...] [--threads 1,2,4] [--seconds S] [--blocks B]"
                         " [--turbo-tolerance 1.10] [--out katalog]\n";
            std::exit(arg == "--help" ? 0 : 2);
        }
    }
    const auto capacity = static_cast<std::size_t>(Tank::columns) * static_cast<std::size_t>(Tank::floor - 2);
    for (const auto n : o.counts)
        if (n == 0 || n > capacity) {
            std::cerr << "N musi być w zakresie 1.." << capacity << '\n';
            std::exit(2);
        }
    for (const int t : o.threads)
        if (t < 1 || t > 64) {
            std::cerr << "Liczba wątków musi być w zakresie 1..64\n";
            std::exit(2);
        }
    return o;
}

double median(std::vector<double> v) {
    if (v.empty()) return std::nan("");
    std::ranges::sort(v);
    const auto m = v.size() / 2;
    return v.size() % 2 ? v[m] : .5 * (v[m - 1] + v[m]);
}

// CPU clock probe: a fixed chain of dependent integer operations on the calling thread, so its time is proportional
// to the core clock. Run before every tick; this laptop switches between turbo and base clock (~1.7x) during a run.
volatile std::uint64_t probe_sink = 0;
double probe_us() {
    const auto t0 = std::chrono::steady_clock::now();
    std::uint64_t x = 0x9E3779B97F4A7C15ULL ^ probe_sink;
    for (int k = 0; k < 1 << 16; ++k) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    probe_sink = x;
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

struct Tick {
    double probe_us{}, density_ms{}, correction_ms{}, sort_ms{}, passes_ms{}, tick_ms{};
};
struct Segment {
    std::size_t n{};
    int threads{}, block{};
    std::vector<Tick> ticks;
    Solver::Stats stats;
    std::uint64_t checksum{};
};

Segment measure(const Options& o, std::size_t n, int threads, int block) {
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    Solver solver{n};
    Pool pool{threads};
    Segment s{.n = n, .threads = threads, .block = block};
    // One tick: per substep a sort (the GPU re-bins once per substep in the default `reused` neighbour mode), then
    // `iterations` x (density, correction). The state reset and the clock probe are outside the timed region.
    auto tick = [&] {
        solver.reset();
        Tick t{.probe_us = probe_us()};
        std::vector<double> density, correction;
        for (int half = 0; half < o.substeps; ++half) {
            const auto t0 = clock::now();
            solver.sort();
            t.sort_ms += ms(clock::now() - t0);
            for (int it = 0; it < o.iterations; ++it) {
                const auto a = clock::now();
                solver.density_pass(pool);
                const auto b = clock::now();
                solver.correction_pass(pool);
                const auto c = clock::now();
                density.push_back(ms(b - a));
                correction.push_back(ms(c - b));
                t.passes_ms += ms(c - a);
            }
        }
        t.density_ms = median(density);
        t.correction_ms = median(correction);
        t.tick_ms = t.passes_ms + t.sort_ms;
        return t;
    };
    const auto warmup_end = clock::now() + std::chrono::duration<double>(.2 * o.seconds);
    for (int k = 0; k < 5 || clock::now() < warmup_end; ++k) tick();
    const auto end = clock::now() + std::chrono::duration<double>(o.seconds);
    while (s.ticks.size() < 20 || clock::now() < end) s.ticks.push_back(tick());
    // Deterministic final state: one fresh tick, then the diagnostics on the same state.
    tick();
    s.checksum = solver.checksum();
    s.stats = solver.stats();
    return s;
}

} // namespace
} // namespace strefa::experiments

int main(int argc, char** argv) {
    using namespace strefa::experiments;
    const Options o = parse(argc, argv);
    std::error_code error;
    std::filesystem::create_directories(o.out, error);
    std::ofstream ticks_csv{o.out / "ticks.csv"}, segments_csv{o.out / "segments.csv"},
            summary_csv{o.out / "summary.csv"};
    if (error || !ticks_csv || !segments_csv || !summary_csv) {
        std::cerr << "Nie można zapisać wyników w " << o.out.string() << '\n';
        return 1;
    }
    std::cerr << "hardware_concurrency=" << std::thread::hardware_concurrency() << " substeps=" << o.substeps
              << " iterations=" << o.iterations << " seconds=" << o.seconds << " blocks=" << o.blocks
              << " turbo_tolerance=" << o.turbo_tolerance << '\n';
    ticks_csv << "block,n,threads,tick,probe_us,density_pass_ms,correction_pass_ms,sort_ms,passes_ms,tick_ms\n";
    segments_csv << "block,n,threads,ticks,probe_median_us,tick_median_ms,mean_neighbors,mean_density_ratio,"
                    "max_density_ratio,checksum\n";
    // Configurations in ABBA order across blocks: even blocks forward, odd blocks reversed.
    std::vector<std::pair<std::size_t, int>> plan;
    for (const auto n : o.counts)
        for (const int t : o.threads) plan.emplace_back(n, t);
    std::vector<Segment> segments;
    for (int block = 0; block < o.blocks; ++block) {
        auto order = plan;
        if (block % 2) std::ranges::reverse(order);
        for (const auto& [n, t] : order) {
            Segment s = measure(o, n, t, block);
            std::vector<double> probes, totals;
            for (std::size_t k = 0; k < s.ticks.size(); ++k) {
                const Tick& x = s.ticks[k];
                probes.push_back(x.probe_us);
                totals.push_back(x.tick_ms);
                char line[256];
                std::snprintf(line, sizeof line, "%d,%zu,%d,%zu,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f\n", block, n, t, k,
                              x.probe_us, x.density_ms, x.correction_ms, x.sort_ms, x.passes_ms, x.tick_ms);
                ticks_csv << line;
            }
            char line[256];
            std::snprintf(line, sizeof line, "%d,%zu,%d,%zu,%.3f,%.6f,%.3f,%.6f,%.6f,%016llx\n", block, n, t,
                          s.ticks.size(), median(probes), median(totals), s.stats.neighbors, s.stats.mean_density,
                          s.stats.max_density, static_cast<unsigned long long>(s.checksum));
            segments_csv << line;
            segments_csv.flush();
            std::cerr << line;
            segments.push_back(std::move(s));
        }
    }
    // The passes are Jacobi with a sequential sort, so every thread count must reach the same bits.
    int status = 0;
    for (const auto& a : segments)
        for (const auto& b : segments)
            if (a.n == b.n && a.checksum != b.checksum) {
                std::cerr << "BŁĄD: różne sumy kontrolne dla N=" << a.n << " (" << a.threads << " i " << b.threads
                          << " wątków)\n";
                status = 1;
            }
    // Ticks whose probe is within turbo_tolerance of the fastest probe of the whole run ran at full clock; ticks at
    // >= 1.5x the fastest probe ran at (about) base clock. Both are reported; the comparison uses the turbo ticks.
    double best = std::numeric_limits<double>::infinity();
    for (const auto& s : segments)
        for (const auto& t : s.ticks) best = std::min(best, t.probe_us);
    summary_csv << std::setprecision(6)
                << "n,threads,ticks,turbo_ticks,base_ticks,density_pass_ms,correction_pass_ms,sort_ms,passes_ms,"
                   "tick_ms,tick_all_ms,tick_base_ms\n";
    std::cerr << "\nnajszybsza sonda: " << best << " us\n"
              << "Mediana ticków przy pełnym zegarze [ms]: N, wątki, ticki (turbo/bazowy), gęstość, korekta, "
                 "sortowanie, 16+16, tick z sortowaniem | wszystkie | zegar bazowy\n";
    for (const auto& [n, t] : plan) {
        std::vector<double> d, c, so, p, k, all, base;
        std::size_t count = 0;
        for (const auto& s : segments)
            if (s.n == n && s.threads == t)
                for (const auto& x : s.ticks) {
                    ++count;
                    all.push_back(x.tick_ms);
                    if (x.probe_us >= 1.5 * best) base.push_back(x.tick_ms);
                    if (x.probe_us > o.turbo_tolerance * best) continue;
                    d.push_back(x.density_ms);
                    c.push_back(x.correction_ms);
                    so.push_back(x.sort_ms);
                    p.push_back(x.passes_ms);
                    k.push_back(x.tick_ms);
                }
        summary_csv << n << ',' << t << ',' << count << ',' << k.size() << ',' << base.size() << ',' << median(d) << ','
                    << median(c) << ',' << median(so) << ',' << median(p) << ',' << median(k) << ',' << median(all)
                    << ',' << median(base) << '\n';
        std::fprintf(stderr, "%6zu %2d %4zu/%-4zu %9.4f %9.4f %9.4f %9.3f %9.3f | %9.3f | %9.3f\n", n, t, k.size(),
                     base.size(), median(d), median(c), median(so), median(p), median(k), median(all), median(base));
    }
    return status;
}
