#pragma once
#include "gpu/shader_cache.hpp"
#include "gpu/shader_library.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace strefa::platform {
class Window;
} // namespace strefa::platform
namespace strefa::ui {
class Session;
} // namespace strefa::ui
namespace strefa::gpu {
class RenderDevice;
} // namespace strefa::gpu

namespace strefa::app {
// Lives while a GPU mode opens: draws "Kompilacja shaderów… n/m" at most every 100 ms while
// programs build and keeps handling window events. Closing the window requests quit and, from
// watch() on, cancels the opening: a close during the VFX programs (built in the RenderDevice
// constructor, which cannot be cancelled) lets them finish and cancels at the first world kernel.
class ShaderProgressScreen {
public:
    ShaderProgressScreen(platform::Window& window, ui::Session& gui, float ui_scale, std::function<void()> on_quit);
    ~ShaderProgressScreen();
    ShaderProgressScreen(const ShaderProgressScreen&) = delete;
    ShaderProgressScreen& operator=(const ShaderProgressScreen&) = delete;
    // For programs built before the device exists (RenderDevice constructor); never cancels.
    [[nodiscard]] gpu::detail::ShaderProgress callback();
    // Reports the device's library from now on and restores its GL state cache after each frame.
    void watch(const std::shared_ptr<gpu::RenderDevice>& device);
    // Ends the screen once the world is open: later programs (thermal) build without frames.
    // Returns the frames drawn since the screen was created or last released.
    unsigned release();

private:
    void update(unsigned done, unsigned total);
    platform::Window& window_;
    ui::Session& gui_;
    float ui_scale_;
    std::function<void()> on_quit_;
    std::shared_ptr<gpu::RenderDevice> device_;
    std::uint64_t last_frame_ns_;
    unsigned frames_{};
    bool quit_{}, cancellable_{};
};

struct GpuStartupTimes {
    unsigned world_creations{}, view_switches{};
    double first_world_ms{}, view_switch_total_ms{}, view_switch_max_ms{};
    bool cpu_laboratory_created{};
    unsigned progress_frames{}; // "Kompilacja shaderów" frames drawn while GPU modes opened
};
// gpu-startup.txt: written right after the first GPU world opens and again at exit.
void write_gpu_startup(const std::filesystem::path& file, const gpu::ShaderCacheStats& cache,
                       const GpuStartupTimes& times);
} // namespace strefa::app
