#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
namespace strefa::platform {
enum class DebugPhase { initialization, runtime, capture, validation, shutdown };
// Process-wide state of the debug log, header-only so GPU benchmarks can label their segments and count buffer
// migrations without a GlDebugLog handle or a link to strefa_platform.
inline std::atomic<DebugPhase> gl_debug_phase{DebugPhase::initialization};
inline std::atomic<std::uint64_t> gl_debug_migrations{}; // messages 131186 received in any phase
inline std::atomic<bool> gl_debug_attached{};            // a GlDebugLog with a driver callback exists
// Bounded callback storage; writing the report happens outside driver callbacks.
class GlDebugLog {
public:
    static std::unique_ptr<GlDebugLog> create(const std::filesystem::path& output);
    ~GlDebugLog();
    // Labels callback receipt; asynchronous driver messages may describe earlier work.
    void set_phase(DebugPhase phase) noexcept;
    GlDebugLog(const GlDebugLog&) = delete;
    GlDebugLog& operator=(const GlDebugLog&) = delete;

private:
    struct Impl;
    explicit GlDebugLog(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace strefa::platform
