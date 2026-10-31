#include "app/application.hpp"
#include <charconv>
#include <cmath>
#include <string_view>
namespace strefa::app {
namespace {
std::filesystem::path utf8_path(const char* text){return std::filesystem::path{reinterpret_cast<const char8_t*>(text)};}
}
std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv) {
    LaunchOptions result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if(argument=="--gpu-lighting-check")result.lighting_check=true;
        else if(argument=="--gpu-lighting-benchmark")result.lighting_benchmark=true;
        else if(argument=="--gpu-lighting-quality"&&i+1<argc){const std::string_view value{argv[++i]};result.lighting_quality_set=true;if(value=="off")result.gpu_render.lighting.quality=gpu::LightingQuality::off;else if(value=="low")result.gpu_render.lighting.quality=gpu::LightingQuality::low;else if(value=="high")result.gpu_render.lighting.quality=gpu::LightingQuality::high;else return std::unexpected{"Oświetlenie: off, low lub high."};}
        else if(argument=="--gpu-thermal-check")result.thermal_check=true;
        else if(argument=="--gpu-thermal-benchmark")result.thermal_benchmark=true;
        else if(argument=="--gpu-thermal-scene"&&i+1<argc){const std::string_view scene{argv[++i]};if(scene=="fire")result.thermal_scene=1;else if(scene=="oil")result.thermal_scene=2;else if(scene=="quench")result.thermal_scene=3;else if(scene=="phases")result.thermal_scene=4;else return std::unexpected{"Scena termiki: fire, oil, quench, phases."};result.gpu_thermal.enabled=true;result.start_in_gpu=true;}
        else if(argument=="--gpu-thermal"&&i+1<argc){const std::string_view mode{argv[++i]};if(mode!="on"&&mode!="off")return std::unexpected{"Termika GPU: on lub off."};result.gpu_thermal.enabled=mode=="on";}
        else if(argument=="--gpu-vfx-check")result.vfx_check=true;
        else if(argument=="--gpu-vfx-benchmark")result.vfx_benchmark=true;
        else if(argument=="--gpu-vfx-quality"&&i+1<argc){const std::string_view value{argv[++i]};
            if(value=="off")result.gpu_vfx.quality=gpu::VfxQuality::off;
            else if(value=="low")result.gpu_vfx.quality=gpu::VfxQuality::low;
            else if(value=="high")result.gpu_vfx.quality=gpu::VfxQuality::high;
            else return std::unexpected{"Jakość VFX: off, low lub high."};}
        else if(argument=="--gpu-render-check")result.render_check=true;
        else if(argument=="--gpu-render-benchmark")result.render_benchmark=true;
        else if(argument=="--gpu-render-quality"&&i+1<argc){const std::string_view value{argv[++i]};
            if(value=="basic")result.gpu_render.quality=gpu::RenderQuality::basic;
            else if(value=="standard")result.gpu_render.quality=gpu::RenderQuality::standard;
            else if(value=="high")result.gpu_render.quality=gpu::RenderQuality::high;
            else return std::unexpected{"Jakość renderowania: basic, standard lub high."};}
        else if(argument=="--session-smoke-test")result.session_smoke=true;
        else if(argument=="--shader-cache-check")result.cache_check=true;
        else if(argument=="--shader-cache"&&i+1<argc){
            const std::string_view mode{argv[++i]};
            if(mode!="on"&&mode!="off")return std::unexpected{"Cache programów: on lub off."};
            result.shader_cache=mode=="on";
        }
        else if(argument=="--gpu-fluid-check")result.fluid_check=true;
        else if(argument=="--gpu-fluid-benchmark")result.fluid_benchmark=true;
        else if(argument=="--gpu-fluid-profile"&&i+1<argc){
            const std::string_view v{argv[++i]};
            if(v=="legacy")result.gpu_execution.fluid={};
            else if(v=="balanced")result.gpu_execution.fluid=gpu::FluidOptions::balanced();
            else return std::unexpected{"Profil cieczy: legacy lub balanced (eksperymentalny)."};
        }
        else if((argument=="--gpu-fluid-boundary"||argument=="--gpu-fluid-surface")&&i+1<argc){
            const std::string_view v{argv[++i]};
            if(argument=="--gpu-fluid-boundary"){
                if(v=="unit")result.gpu_execution.fluid.boundary=gpu::FluidBoundary::unit;
                else if(v=="volume")result.gpu_execution.fluid.boundary=gpu::FluidBoundary::volume;
                else return std::unexpected{"Granice cieczy: unit lub volume."};
            }else{
                if(v=="legacy")result.gpu_execution.fluid.surface=gpu::FluidSurface::legacy;
                else if(v=="akinci2d")result.gpu_execution.fluid.surface=gpu::FluidSurface::akinci2d;
                else return std::unexpected{"Napięcie cieczy: legacy lub akinci2d."};
            }
        }
        else if(argument=="--gpu-fluid-substeps"&&i+1<argc){
            const std::string_view v{argv[++i]};auto& f=result.gpu_execution.fluid;
            if(v=="2"){f.substeps=2;f.pbf_iterations=8;f.contact_iterations=12;}
            else if(v=="4"){f.substeps=4;f.pbf_iterations=4;f.contact_iterations=6;}
            else return std::unexpected{"Podkroki cieczy: 2 lub 4."};
        }
        else if((argument=="--gpu-fluid-s-corr"||argument=="--gpu-fluid-tension-scale"||argument=="--gpu-fluid-curvature-scale"
            ||argument=="--gpu-fluid-adhesion"||argument=="--gpu-fluid-vorticity")&&i+1<argc){
            const std::string_view v{argv[++i]};float value{};
            const auto parsed=std::from_chars(v.data(),v.data()+v.size(),value);
            if(parsed.ec!=std::errc{}||parsed.ptr!=v.data()+v.size()||!std::isfinite(value))return std::unexpected{"Nieprawidłowy współczynnik cieczy."};
            auto& f=result.gpu_execution.fluid;
            if(argument=="--gpu-fluid-s-corr")f.s_corr=value;
            else if(argument=="--gpu-fluid-tension-scale")f.tension_scale=value;
            else if(argument=="--gpu-fluid-curvature-scale")f.curvature_scale=value;
            else if(argument=="--gpu-fluid-adhesion")f.adhesion=value;
            else f.vorticity=value;
        }
        else if (argument == "--smoke-test") result.smoke_test = true;
        else if(argument=="--lab-smoke-test") result.laboratory_smoke_test=true;
        else if (argument == "--laboratory") result.start_in_laboratory = true;
        else if (argument == "--arena") result.start_in_arena = true;
        else if(argument=="--gpu-laboratory")result.start_in_gpu=true;
        else if(argument=="--mission")result.start_in_mission=true;
        else if(argument=="--gameplay-check"){result.mission_test=true;result.gameplay_test=true;}
        else if(argument=="--mission-test")result.mission_test=true;
        else if(argument=="--mission-smoke-test")result.mission_smoke=true;
        else if(argument=="--gpu-test")result.gpu_test=true;
        else if(argument=="--gpu-liquid-test")result.gpu_liquid_test=true;
        else if(argument=="--gpu-flow-test")result.gpu_flow_test=true;
        else if(argument=="--gpu-smoke-test")result.gpu_smoke_test=true;
        else if(argument=="--opengl33")result.legacy_graphics=true;
        else if(argument=="--gpu-work-check")result.work_test=true;
        else if(argument=="--gpu-transport-check")result.transport_test=true;
        else if(argument=="--gpu-hot-check")result.hot_test=true;
        else if(argument=="--gpu-cost-profile")result.cost_profile=true;
        else if(argument=="--gpu-cost-check")result.cost_check=true;
        else if(argument=="--gpu-cost-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};bool found=false;
            for(const auto mode:{gpu::CostMode::off,gpu::CostMode::passes,gpu::CostMode::bounds,gpu::CostMode::snapshots})
                if(value==gpu::cost_mode_name(mode)){result.cost_mode=mode;found=true;}
            if(!found)return std::unexpected{"Diagnostyka kosztu GPU: off, passes, bounds lub snapshots."};
        }
        else if(argument=="--gpu-neighbor-reads"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="full")result.gpu_execution.neighbor_reads=gpu::NeighborReads::full;
            else if(value=="narrow")result.gpu_execution.neighbor_reads=gpu::NeighborReads::narrow;
            else if(value=="sorted")result.gpu_execution.neighbor_reads=gpu::NeighborReads::sorted;
            else return std::unexpected{"Odczyty sąsiadów: full, narrow lub sorted."};
        }
        else if(argument=="--gpu-storage"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="direct")result.gpu_execution.storage=gpu::DeviceStorageMode::direct;
            else if(value=="staged")result.gpu_execution.storage=gpu::DeviceStorageMode::staged;
            else if(value=="immutable")result.gpu_execution.storage=gpu::DeviceStorageMode::immutable;
            else if(value=="auto")result.gpu_execution.storage=gpu::DeviceStorageMode::automatic;
            else return std::unexpected{"Pamięć GPU: direct, staged, immutable lub auto."};
        }
        else if((argument=="--gpu-phase-gating"||argument=="--gpu-zero-projection")&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="on"&&value!="off")return std::unexpected{"Optymalizacja pracy GPU: on lub off."};
            if(argument=="--gpu-phase-gating")result.gpu_execution.phase_gating=value=="on";
            else result.gpu_execution.skip_zero_projection=value=="on";
        }
        else if(argument=="--gpu-coupling-check")result.coupling_test=true;
        else if(argument=="--gpu-coupling-control"){result.coupling_test=true;result.coupling_reference_control=true;}
        else if(argument=="--gpu-coupling-legacy-check"){result.coupling_test=true;result.coupling_legacy_check=true;}
        else if(argument=="--gpu-coupling-profile")result.coupling_profile=true;
        else if(argument=="--gpu-coupling"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="reference")result.gpu_execution.coupling=gpu::CouplingMode::reference;
            else if(value=="local")result.gpu_execution.coupling=gpu::CouplingMode::local;
            else if(value=="combined")result.gpu_execution.coupling=gpu::CouplingMode::combined;
            else return std::unexpected{"Sprzężenie GPU: reference, local lub combined."};
        }
        else if(argument=="--gpu-neighbor-check")result.neighbor_test=true;
        else if(argument=="--gpu-neighbor-profile")result.neighbor_profile=true;
        else if(argument=="--gpu-neighbors"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="linked")result.gpu_execution.neighbors=gpu::NeighborMode::linked;
            else if(value=="sorted")result.gpu_execution.neighbors=gpu::NeighborMode::sorted;
            else if(value=="reused")result.gpu_execution.neighbors=gpu::NeighborMode::reused;
            else return std::unexpected{"Sąsiedztwo GPU: linked, sorted lub reused."};
        }
        else if(argument=="--gpu-layout"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="aos"&&value!="soa")return std::unexpected{"Układ cząstek GPU: aos lub soa."};
            result.gpu_execution.soa=value=="soa";
        }
        else if(argument=="--gpu-execution-check")result.execution_test=true;
        else if(argument=="--gpu-pbf-restrict-check"){result.execution_test=true;result.pbf_restrict_test=true;}
        else if(argument=="--gpu-execution-profile")result.execution_profile=true;
        else if(argument=="--gpu-execution"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="reference")result.gpu_execution=gpu::ExecutionOptions::reference();
            else if(value=="optimized")result.gpu_execution={};
            else return std::unexpected{"Harmonogram GPU: reference lub optimized."};
        }
        else if(argument=="--gpu-pbf-restrict"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="on"&&value!="off")return std::unexpected{"Brak aliasowania PBF: on lub off."};
            result.gpu_execution.pbf_restrict=value=="on";
        }
        else if((argument=="--gpu-local-size"||argument=="--gpu-reduction-size"||argument=="--gpu-pbf-size")&&i+1<argc){
            const std::string_view value{argv[++i]};unsigned size{};
            if(value=="64")size=64;else if(value=="128")size=128;else if(value=="256")size=256;
            else return std::unexpected{"Rozmiar grupy GPU: 64, 128 lub 256."};
            if(argument=="--gpu-local-size")result.gpu_execution.local_size=size;
            else if(argument=="--gpu-pbf-size")result.gpu_execution.pbf_size=size;
            else result.gpu_execution.reduction_size=size;
        }
        else if(argument=="--gpu-readback-check")result.readback_test=true;
        else if(argument=="--gpu-readback-profile")result.readback_profile=true;
        else if(argument=="--gpu-readback-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};bool found=false;
            for(const auto mode:{gpu::ReadbackMode::automatic,gpu::ReadbackMode::legacy,gpu::ReadbackMode::stream,gpu::ReadbackMode::staging,gpu::ReadbackMode::persistent})
                if(value==gpu::readback_mode_name(mode)){result.gpu_profiling.readback_mode=mode;found=true;}
            if(!found)return std::unexpected{"Tryb odczytu GPU: auto, legacy, stream, staging lub persistent."};
        }
        else if(argument=="--gpu-profile")result.gpu_profile=true;
        else if(argument=="--gpu-stabilization-profile")result.stabilization_profile=true;
        else if(argument=="--gpu-profile-check")result.gpu_profile_check=true;
        else if(argument=="--gl-debug")result.gl_debug=true;
        else if(argument=="--show-test-window")result.show_test_window=true;
        else if(argument=="--gpu-counters")result.gpu_profiling.collect=true;
        else if(argument=="--gpu-timer-mode"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value=="elapsed")result.gpu_profiling.timer_mode=gpu::TimerMode::elapsed;
            else if(value=="timestamp")result.gpu_profiling.timer_mode=gpu::TimerMode::timestamp;
            else if(value=="off")result.gpu_profiling.timer_mode=gpu::TimerMode::off;
            else return std::unexpected{"Tryb timerów GPU: elapsed, timestamp lub off."};
            result.gpu_profiling.gpu_timers=value!="off";
        }
        else if(argument=="--test-unpaced")result.test_unpaced=true;
        else if(argument=="--test-window-size"&&i+2<argc){std::array<int,2> size{};
            for(unsigned axis=0;axis<2;++axis){const std::string_view value{argv[++i]};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),size[axis]);
                if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())return std::unexpected{"Nieprawidłowy rozmiar okna testowego."};}
            result.test_window_size=size;}
        else if(argument=="--test-vsync"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="on"&&value!="off")return std::unexpected{"Test VSync: on lub off."};
            result.test_vsync=value=="on";
        }
        else if(argument=="--test-frame-limit"&&i+1<argc){
            const std::string_view value{argv[++i]};unsigned limit{};
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),limit);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||limit<30||limit>240)
                return std::unexpected{"Testowy limit FPS musi wynosić 30–240."};
            result.test_frame_limit=limit;
        }
        else if(argument=="--gpu-group-timers"){result.gpu_profiling.collect=true;result.gpu_profiling.group_timers=true;}
        else if(argument=="--gpu-no-timers"){result.gpu_profiling.gpu_timers=false;result.gpu_profiling.timer_mode=gpu::TimerMode::off;}
        else if(argument=="--gpu-no-coupling")result.gpu_profiling.body_coupling=false;
        else if(argument=="--gpu-no-diagnostics"){result.gpu_profiling.gpu_timers=false;result.gpu_profiling.periodic_diagnostics=false;}
        else if(argument=="--gpu-readback-period"&&i+1<argc){
            const std::string_view value{argv[++i]};
            if(value!="0"&&value!="1"&&value!="2"&&value!="4")return std::unexpected{"Okres odczytu GPU musi wynosić 0, 1, 2 lub 4."};
            result.gpu_profiling.readback_period=static_cast<unsigned>(value.front()-'0');
        }
        else if(argument=="--physics-threads"&&i+1<argc) {
            const std::string_view value{argv[++i]};
            if(value.size()!=1||value.front()<'1'||value.front()>'4')
                return std::unexpected{"Liczba wątków fizyki musi wynosić 1–4."};
            result.physics_threads=static_cast<unsigned>(value.front()-'0');
        }
        else if(argument=="--test-seconds"&&i+1<argc){const std::string_view value{argv[++i]};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result.test_seconds);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||result.test_seconds<1||result.test_seconds>120)return std::unexpected{"Czas testu: 1–120 s."};}
        else if (argument == "--profile" && i + 1 < argc) result.profile = utf8_path(argv[++i]);
        else return std::unexpected{"Nieznany albo niepełny argument: " + std::string{argument}};
    }
    if(!result.gpu_execution.fluid.valid())return std::unexpected{"Konfiguracja cieczy poza zakresem I07."};
    if ((result.smoke_test||result.laboratory_smoke_test||result.gpu_test||result.gpu_liquid_test||result.gpu_flow_test||result.gpu_smoke_test||result.mission_test||result.mission_smoke||result.gpu_profile||result.stabilization_profile||result.gpu_profile_check||result.readback_test||result.readback_profile||result.execution_test||result.execution_profile||result.neighbor_test||result.neighbor_profile||result.coupling_test||result.coupling_profile||result.work_test||result.transport_test||result.hot_test||result.cost_profile||result.thermal_check||result.thermal_benchmark||result.vfx_check||result.vfx_benchmark||result.lighting_check||result.lighting_benchmark||result.render_check||result.render_benchmark||result.fluid_check||result.fluid_benchmark||result.cost_check||result.session_smoke||result.cache_check) && result.profile.empty())
        return std::unexpected{"Test wymaga osobnego katalogu: --smoke-test --profile <katalog>."};
    if (static_cast<int>(result.start_in_arena)+result.start_in_laboratory+result.start_in_gpu+result.start_in_mission>1)
        return std::unexpected{"Wybierz jeden tryb startowy."};
    if((result.test_seconds||result.test_unpaced||result.test_window_size.has_value())&&!result.mission_smoke&&!result.gpu_smoke_test)return std::unexpected{"Opcje pomiaru okna wymagają testu misji lub laboratorium GPU."};
    if(result.coupling_reference_control&&result.coupling_legacy_check)return std::unexpected{"Wybierz jedną kontrolę sprzężenia: reference control albo legacy."};
    if(result.cost_profile&&(!result.gpu_profiling.gpu_timers||result.gpu_profiling.timer_mode!=gpu::TimerMode::elapsed||result.gpu_profiling.group_timers
        ||!result.gpu_profiling.periodic_diagnostics||!result.gpu_profiling.body_coupling||result.gpu_profiling.readback_period!=2||result.gpu_profiling.readback_mode!=gpu::ReadbackMode::automatic))
        return std::unexpected{"Profil kosztu ma stały protokół: elapsed, diagnostyka/sprzężenie ON, HUD auto/2, bez timerów grupowych."};
    if(static_cast<int>(result.smoke_test)+result.laboratory_smoke_test+result.gpu_test+result.gpu_liquid_test+result.gpu_flow_test+result.gpu_smoke_test+result.mission_test+result.mission_smoke+result.gpu_profile+result.stabilization_profile+result.gpu_profile_check+result.readback_test+result.readback_profile+result.execution_test+result.execution_profile+result.neighbor_test+result.neighbor_profile+result.coupling_test+result.coupling_profile+result.work_test+result.transport_test+result.hot_test+result.cost_profile+result.cost_check+result.thermal_check+result.thermal_benchmark+result.vfx_check+result.vfx_benchmark+result.lighting_check+result.lighting_benchmark+result.render_check+result.render_benchmark+result.fluid_check+result.fluid_benchmark+result.session_smoke+result.cache_check>1) return std::unexpected{"Wybierz jeden rodzaj testu."};
    if((result.test_vsync.has_value()||result.test_frame_limit.has_value())&&
        !(result.smoke_test||result.laboratory_smoke_test||result.gpu_smoke_test||result.mission_smoke||result.session_smoke))
        return std::unexpected{"Opcje --test-vsync i --test-frame-limit wymagają testu okna."};
    return result;
}

}
