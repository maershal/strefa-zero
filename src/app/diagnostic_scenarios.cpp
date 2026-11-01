#include "app/diagnostic_scenarios.hpp"

#include "app/cpu_vfx.hpp"
#include "gpu/validation.hpp"
#include "platform/texture.hpp"
namespace strefa::app {
std::optional<std::expected<int, std::string>> run_diagnostic_scenario(const LaunchOptions& options,
                                                                       const std::filesystem::path& assets,
                                                                       const std::filesystem::path& profile) {
    if (options.thermal_check || options.thermal_benchmark) {
        auto world = gpu::ParticleWorld::create(assets / "shaders", options.gpu_execution, options.gpu_profiling);
        if (!world) return std::unexpected{world.error()};
        const auto result = (*world)->validate_thermal(profile, options.thermal_benchmark);
        if (!result) return std::unexpected{result.error()};
        if (options.thermal_check) {
            const auto session = validate_thermal_session(assets / "shaders", profile, (*world)->render_device());
            if (!session) return std::unexpected{session.error()};
        }
        return 0;
    }
    if (options.lighting_check || options.lighting_benchmark || options.vfx_check || options.vfx_benchmark
        || options.render_check || options.render_benchmark) {
        auto world = gpu::ParticleWorld::create(assets / "shaders", options.gpu_execution, options.gpu_profiling);
        if (!world) return std::unexpected{world.error()};
        auto parts = platform::Texture::load_bmp(assets / "actors/parts.bmp");
        auto weapons = platform::Texture::load_bmp(assets / "weapons/weapons.bmp");
        auto background = platform::Texture::load_bmp(assets / "backgrounds/industrial.bmp");
        if (parts && weapons)
            (*world)->set_actor_textures(parts->id(), weapons->id(), weapons->width(), weapons->height());
        if (background) (*world)->set_background_texture(background->id());
        (*world)->set_vfx_options(options.gpu_vfx);
        (*world)->set_render_options(options.gpu_render);
        const auto result = (options.lighting_check || options.lighting_benchmark)
                                  ? (*world)->validate_lighting(profile, options.lighting_benchmark)
                          : (options.vfx_check || options.vfx_benchmark)
                                  ? (*world)->validate_vfx(profile, options.vfx_benchmark)
                                  : (*world)->validate_renderer(profile, options.render_benchmark);
        if (!result) return std::unexpected{result.error()};
        if (options.vfx_check) {
            const auto cpu = validate_cpu_vfx(*(*world)->render_device(), profile);
            if (!cpu) return std::unexpected{cpu.error()};
        }
        return 0;
    }
    if (options.fluid_check || options.fluid_benchmark) {
        const auto result = gpu::validate_fluid_quality(assets / "shaders", profile, options.gpu_execution,
                                                        options.fluid_benchmark);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.cache_check) return validate_shader_cache(profile);
    if (options.work_test) {
        const auto result = gpu::validate_work(profile, options.gpu_execution);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.transport_test) {
        const auto result = gpu::validate_transport(profile);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.cost_profile || options.cost_check) {
        const auto result = gpu::profile_cost(profile, options.gpu_execution, options.cost_mode, options.cost_check);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.hot_test) {
        const auto result = gpu::validate_hot_reads(profile, options.gpu_execution);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.coupling_test || options.coupling_profile) {
        const auto result =
                gpu::validate_coupling(profile, options.coupling_profile, options.coupling_reference_control,
                                       options.coupling_legacy_check, options.gpu_execution);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.neighbor_test || options.neighbor_profile) {
        const auto result = gpu::validate_neighbors(profile, options.neighbor_profile);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.execution_test || options.execution_profile) {
        const auto result = options.pbf_restrict_test ? gpu::validate_pbf_restrict(profile)
                                                      : gpu::validate_execution(profile, options.execution_profile);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.readback_test || options.readback_profile) {
        const auto result = options.readback_test ? gpu::validate_readback(profile) : gpu::profile_readback(profile);
        if (!result) return std::unexpected{result.error()};
        return 0;
    }
    if (options.gpu_test || options.gpu_liquid_test || options.gpu_flow_test || options.mission_test
        || options.gpu_profile || options.stabilization_profile || options.gpu_profile_check || options.readback_test
        || options.readback_profile || options.execution_test || options.execution_profile || options.neighbor_test
        || options.neighbor_profile || options.coupling_test || options.coupling_profile || options.work_test
        || options.transport_test || options.hot_test || options.cost_profile || options.cost_check) {
        auto world = gpu::ParticleWorld::create(assets / "shaders", options.gpu_execution, options.gpu_profiling);
        if (!world) return std::unexpected{world.error()};
        (*world)->set_thermal_options(options.gpu_thermal);
        if (options.stabilization_profile) {
            const auto result = gpu::profile_stabilization(**world, profile, options.gpu_profiling);
            if (!result) return std::unexpected{result.error()};
            return 0;
        }
        if (options.gpu_profile || options.gpu_profile_check) {
            const auto result = gpu::profile_gpu(**world, profile, options.gpu_profile_check);
            if (!result) return std::unexpected{result.error()};
            return 0;
        }
        const auto validated = options.gameplay_test   ? (*world)->validate_gameplay_protocol(profile)
                             : options.mission_test    ? gpu::validate_mission(**world, profile)
                             : options.gpu_flow_test   ? gpu::validate_flow(**world, profile)
                             : options.gpu_liquid_test ? gpu::validate_liquids(**world, profile)
                                                       : gpu::validate_granular(**world, profile);
        if (!validated) return std::unexpected{validated.error()};
        return 0;
    }
    return std::nullopt;
}
} // namespace strefa::app
