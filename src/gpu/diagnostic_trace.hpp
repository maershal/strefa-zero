#pragma once
#include "gpu/diagnostics.hpp"
#include "gpu/gl_api.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace strefa::gpu::detail {
// Explicit diagnostic capture. No resources or GL calls while disabled.
// read() is called only after the CLI's explicit out-of-band GPU drain.
class DiagnosticTrace {
    Api& gl_;
    TraceConfig config_;
    bool enabled_{}, in_step_{};
    unsigned ordinal_{}, previous_count_{1}, target_{}, samples_{};
    int phase_{-1}, iteration_{-1};
    GLuint headers_{}, snapshots_{};
    std::size_t particle_bytes_{}, cell_bytes_{}, index_bytes_{}, capture_bytes_{};
    std::uint64_t header_copies_{};
    std::vector<GLuint> queries_;
    std::vector<unsigned> query_slots_;
    std::vector<DispatchSample> events_;
    std::vector<SpatialCapture> captures_;
    static constexpr auto invalid = std::numeric_limits<std::size_t>::max();
    void release() {
        if (headers_) gl_.DeleteBuffers(1, &headers_);
        if (snapshots_) gl_.DeleteBuffers(1, &snapshots_);
        if (!queries_.empty()) gl_.DeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
        headers_ = snapshots_ = 0;
        queries_.clear();
        enabled_ = false;
    }
    void allocate(GLuint& buffer, std::size_t bytes) {
        gl_.GenBuffers(1, &buffer);
        gl_.BindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        gl_.BufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
    }
    void copy(GLuint source, GLuint destination, std::size_t read_offset, std::size_t write_offset, std::size_t bytes) {
        gl_.BindBuffer(GL_COPY_READ_BUFFER, source);
        gl_.BindBuffer(GL_COPY_WRITE_BUFFER, destination);
        gl_.CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(read_offset),
                              static_cast<GLintptr>(write_offset), static_cast<GLsizeiptr>(bytes));
    }

public:
    explicit DiagnosticTrace(Api& gl) : gl_{gl} {}
    ~DiagnosticTrace() { release(); }
    bool enabled() const { return enabled_; }
    void start(const TraceConfig& config, std::size_t particle_bytes, std::size_t cell_bytes, std::size_t index_bytes) {
        if (enabled_) throw std::logic_error{"A diagnostic trace is already active"};
        release();
        config_ = config;
        events_.clear();
        captures_.clear();
        query_slots_.clear();
        samples_ = 0;
        header_copies_ = 0;
        ordinal_ = 0;
        previous_count_ = 1;
        target_ = 0;
        phase_ = iteration_ = -1;
        in_step_ = false;
        particle_bytes_ = particle_bytes;
        cell_bytes_ = cell_bytes;
        index_bytes_ = index_bytes;
        capture_bytes_ = particle_bytes + cell_bytes + index_bytes;
        if (config.mode == CostMode::off) return;
        if (config.max_events == 0 || config.max_events > 1000000 || config.max_samples > 10000
            || config.max_snapshots > 64)
            throw std::invalid_argument{"Invalid diagnostic trace capacity"};
        if (config.mode == CostMode::passes && config.max_samples == 0)
            throw std::invalid_argument{"Pass trace needs query slots"};
        events_.reserve(config.max_events);
        query_slots_.reserve(config.max_events);
        captures_.reserve(config.max_snapshots);
        if (config.mode == CostMode::passes) {
            queries_.resize(2 * config.max_samples);
            gl_.GenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
        }
        if (config.mode == CostMode::bounds || config.mode == CostMode::snapshots)
            allocate(headers_, config.max_events * 8ULL);
        if (config.mode == CostMode::snapshots && config.max_snapshots)
            allocate(snapshots_, config.max_snapshots * capture_bytes_);
        enabled_ = true;
    }
    void begin_step(std::uint64_t tick) {
        if (!enabled_) return;
        ordinal_ = 0;
        target_ = static_cast<unsigned>((tick * 47) % previous_count_);
        in_step_ = true;
        context(0, -1);
    }
    void context(int phase, int iteration) {
        if (enabled_) {
            phase_ = phase;
            iteration_ = iteration;
        }
    }
    void end_step() {
        if (enabled_) {
            previous_count_ = std::max(ordinal_, 1U);
            in_step_ = false;
            context(-1, -1);
        }
    }
    std::size_t begin(PassFamily family, unsigned operation, unsigned generation, std::uint64_t tick) {
        if (!enabled_) return invalid;
        if (events_.size() == config_.max_events)
            throw std::runtime_error{"Diagnostic event capacity exceeded; trace is incomplete"};
        DispatchSample event;
        event.family = family;
        event.operation = operation;
        event.generation = generation;
        event.tick = tick;
        event.ordinal = ordinal_++;
        event.phase = phase_;
        event.iteration = iteration_;
        unsigned query = std::numeric_limits<unsigned>::max();
        if (config_.mode == CostMode::passes && in_step_ && event.ordinal == target_) {
            if (samples_ == config_.max_samples)
                throw std::runtime_error{"Diagnostic query capacity exceeded; trace is incomplete"};
            query = samples_++;
            event.sampled = true;
            gl_.QueryCounter(queries_[2 * query], GL_TIMESTAMP);
        }
        events_.push_back(event);
        query_slots_.push_back(query);
        return events_.size() - 1;
    }
    void capture(std::string label, unsigned generation, std::uint64_t tick, unsigned ordinal, bool soa,
                 GLuint particles, GLuint cells, GLuint index) {
        if (!enabled_ || config_.mode != CostMode::snapshots) return;
        if (captures_.size() == config_.max_snapshots)
            throw std::runtime_error{"Diagnostic snapshot capacity exceeded; trace is incomplete"};
        const auto offset = captures_.size() * capture_bytes_;
        gl_.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        copy(particles, snapshots_, 0, offset, particle_bytes_);
        copy(cells, snapshots_, 0, offset + particle_bytes_, cell_bytes_);
        copy(index, snapshots_, 0, offset + particle_bytes_ + cell_bytes_, index_bytes_);
        SpatialCapture value;
        value.label = std::move(label);
        value.tick = tick;
        value.generation = generation;
        value.ordinal = ordinal;
        value.phase = phase_;
        value.iteration = iteration_;
        value.soa = soa;
        captures_.push_back(std::move(value));
    }
    void end(std::size_t token, bool indexed, bool soa, GLuint particles, GLuint cells, GLuint index) {
        if (token == invalid) return;
        auto& event = events_[token];
        if (event.sampled) gl_.QueryCounter(queries_[2 * query_slots_[token] + 1], GL_TIMESTAMP);
        event.indexed = indexed;
        if (headers_ && indexed) {
            gl_.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            copy(cells, headers_, 0, token * 8, 8);
            ++header_copies_;
        }
        const bool snapshot_tick =
                std::ranges::find(config_.snapshot_ticks, event.tick) != config_.snapshot_ticks.end();
        const bool checkpoint = (event.family == PassFamily::neighbors && event.operation == 4)
                             || (event.family == PassFamily::granular && event.operation == 3
                                 && (iteration_ == 0 || iteration_ == 11));
        if (config_.mode == CostMode::snapshots && in_step_ && snapshot_tick && checkpoint && indexed)
            capture(event.family == PassFamily::neighbors ? "index_built" : "after_contact", event.generation,
                    event.tick, event.ordinal, soa, particles, cells, index);
    }
    TraceReport read() {
        enabled_ = false;
        TraceReport report;
        report.mode = config_.mode;
        report.timestamp_pairs = samples_;
        report.header_copies = header_copies_;
        report.snapshot_copies = 3 * captures_.size();
        report.staging_bytes =
                (headers_ ? config_.max_events * 8ULL : 0) + (snapshots_ ? config_.max_snapshots * capture_bytes_ : 0);
        for (std::size_t i = 0; i < events_.size(); ++i)
            if (events_[i].sampled) {
                const auto query = query_slots_[i];
                GLint available{};
                gl_.GetQueryObjectiv(queries_[2 * query + 1], GL_QUERY_RESULT_AVAILABLE, &available);
                if (!available) throw std::runtime_error{"Drain GPU before reading diagnostic timestamps"};
                GLuint64 begin{}, end{};
                gl_.GetQueryObjectui64v(queries_[2 * query], GL_QUERY_RESULT, &begin);
                gl_.GetQueryObjectui64v(queries_[2 * query + 1], GL_QUERY_RESULT, &end);
                if (end < begin) throw std::runtime_error{"Nonmonotonic diagnostic timestamp pair"};
                events_[i].milliseconds = static_cast<double>(end - begin) * 1e-6;
            }
        if (headers_ && !events_.empty()) {
            std::vector<std::array<std::uint32_t, 2>> values(events_.size());
            gl_.BindBuffer(GL_COPY_READ_BUFFER, headers_);
            gl_.GetBufferSubData(GL_COPY_READ_BUFFER, 0, static_cast<GLsizeiptr>(values.size() * 8), values.data());
            for (std::size_t i = 0; i < events_.size(); ++i)
                if (events_[i].indexed) {
                    events_[i].displacement = std::bit_cast<float>(values[i][0]);
                    events_[i].builds = values[i][1];
                }
        }
        if (snapshots_)
            for (std::size_t i = 0; i < captures_.size(); ++i) {
                auto& value = captures_[i];
                value.particles.resize(particle_bytes_);
                value.cells.resize(cell_bytes_ / 4);
                value.index.resize(index_bytes_ / 4);
                const auto offset = i * capture_bytes_;
                gl_.BindBuffer(GL_COPY_READ_BUFFER, snapshots_);
                gl_.GetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(offset),
                                     static_cast<GLsizeiptr>(particle_bytes_), value.particles.data());
                gl_.GetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(offset + particle_bytes_),
                                     static_cast<GLsizeiptr>(cell_bytes_), value.cells.data());
                gl_.GetBufferSubData(GL_COPY_READ_BUFFER, static_cast<GLintptr>(offset + particle_bytes_ + cell_bytes_),
                                     static_cast<GLsizeiptr>(index_bytes_), value.index.data());
            }
        report.dispatches = std::move(events_);
        report.captures = std::move(captures_);
        release();
        config_ = {};
        samples_ = 0;
        header_copies_ = 0;
        return report;
    }
};
} // namespace strefa::gpu::detail
