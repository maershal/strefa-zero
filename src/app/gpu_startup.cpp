#include "app/gpu_startup.hpp"

#include "gpu/render_device.hpp"
#include "platform/window.hpp"
#include "ui/session.hpp"
#include "ui/shader_progress.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <stdexcept>

namespace strefa::app {
namespace {
constexpr std::uint64_t frame_interval_ns = 100'000'000; // the first frame too: a cached start shows ~none
} // namespace
ShaderProgressScreen::ShaderProgressScreen(platform::Window& window, ui::Session& gui, float ui_scale,
                                           std::function<void()> on_quit)
    : window_{window},
      gui_{gui},
      ui_scale_{ui_scale},
      on_quit_{std::move(on_quit)},
      last_frame_ns_{SDL_GetTicksNS()} {}
ShaderProgressScreen::~ShaderProgressScreen() {
    release();
}
void ShaderProgressScreen::release() {
    if (!device_) return;
    device_->library.set_progress({});
    device_->context.state.invalidate();
    device_.reset();
}
gpu::detail::ShaderProgress ShaderProgressScreen::callback() {
    return [this](unsigned done, unsigned total) { update(done, total); };
}
void ShaderProgressScreen::watch(const std::shared_ptr<gpu::RenderDevice>& device) {
    if (!device) return;
    device_ = device;
    device_->context.state.invalidate();
    device_->library.set_progress(callback());
    cancellable_ = true;
}
void ShaderProgressScreen::update(unsigned done, unsigned total) {
    const auto cancel = [&] {
        // ParticleWorld::create turns this into an error; the application then ends its loop.
        if (quit_ && cancellable_)
            throw std::runtime_error{"Otwieranie trybu GPU przerwano: okno zamknięto podczas kompilacji shaderów."};
    };
    cancel();
    const auto now = SDL_GetTicksNS();
    if (quit_ || now - last_frame_ns_ < frame_interval_ns) return;
    last_frame_ns_ = now;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        gui_.process_event(event);
        if (event.type == SDL_EVENT_QUIT
            || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                && event.window.windowID == SDL_GetWindowID(window_.native()))) {
            quit_ = true;
            if (on_quit_) on_quit_();
        }
    }
    cancel();
    if (quit_ || window_.minimized()) return;
    gui_.set_scale(window_.display_scale(), ui_scale_);
    gui_.new_frame();
    ui::draw_shader_progress(done, total, gui_.scale());
    window_.clear();
    gui_.render();
    window_.present();
    if (device_) device_->context.state.invalidate();
}
void write_gpu_startup(const std::filesystem::path& file, const gpu::ShaderCacheStats& cache,
                       const GpuStartupTimes& times) {
    std::ofstream startup{file};
    startup << "world_creations=" << times.world_creations << "\nprograms=" << cache.programs
            << "\nsource_builds=" << cache.source_builds << "\ncache_hits=" << cache.hits
            << "\ncache_misses=" << cache.misses << "\ncache_rejected=" << cache.rejected
            << "\ncache_write_failures=" << cache.write_failures << "\ncache_supported=" << cache.supported
            << "\nparallel_compile=" << cache.parallel << "\ncompile_ms=" << cache.compile_ms
            << "\ncache_load_ms=" << cache.load_ms << "\nsubmit_ms=" << cache.submit_ms
            << "\nprogress_ms=" << cache.progress_ms << "\nprogress_gap_ms=" << cache.progress_gap_ms
            << "\nfirst_world_ms=" << times.first_world_ms << "\nview_switches=" << times.view_switches
            << "\nview_switch_total_ms=" << times.view_switch_total_ms
            << "\nview_switch_max_ms=" << times.view_switch_max_ms
            << "\ncpu_laboratory_created=" << times.cpu_laboratory_created << '\n';
}
} // namespace strefa::app
