#include "gpu/readback_ring.hpp"
#include "gpu/validation.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
using detail::Api;
using detail::ReadbackPacket;
using detail::ReadbackRing;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
class Producer {
    Api& gl_;
    GLuint buffer_{}, program_{};
    GLint seed_{};

public:
    explicit Producer(Api& gl) : gl_{gl} {
        const char* source = R"glsl(#version 430 core
layout(local_size_x=1) in;
layout(std430,binding=0) buffer Output {uint words[76];};
uniform int seed;
void main(){for(uint i=0u;i<76u;++i)words[i]=uint(seed)*1000u+i;}
)glsl";
        const auto shader = gl_.CreateShader(GL_COMPUTE_SHADER);
        gl_.ShaderSource(shader, 1, &source, nullptr);
        gl_.CompileShader(shader);
        GLint valid{};
        gl_.GetShaderiv(shader, GL_COMPILE_STATUS, &valid);
        if (!valid) {
            gl_.DeleteShader(shader);
            throw std::runtime_error{"Readback producer shader failed"};
        }
        program_ = gl_.CreateProgram();
        gl_.AttachShader(program_, shader);
        gl_.LinkProgram(program_);
        gl_.DeleteShader(shader);
        gl_.GetProgramiv(program_, GL_LINK_STATUS, &valid);
        if (!valid) {
            gl_.DeleteProgram(program_);
            throw std::runtime_error{"Readback producer link failed"};
        }
        seed_ = gl_.GetUniformLocation(program_, "seed");
        gl_.GenBuffers(1, &buffer_);
        gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer_);
        gl_.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ReadbackPacket), nullptr, GL_DYNAMIC_COPY);
    }
    ~Producer() {
        gl_.DeleteBuffers(1, &buffer_);
        gl_.DeleteProgram(program_);
    }
    GLuint write(int seed) {
        gl_.UseProgram(program_);
        gl_.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer_);
        gl_.Uniform1i(seed_, seed);
        gl_.DispatchCompute(1, 1, 1);
        return buffer_;
    }
    static void check(const ReadbackPacket& packet, unsigned seed) {
        std::array<std::uint32_t, sizeof(ReadbackPacket) / 4> words{};
        std::memcpy(words.data(), &packet, sizeof(packet));
        for (std::size_t i = 0; i < words.size(); ++i)
            require(words[i] == seed * 1000U + i, "Readback packet torn or overwritten");
    }
};
std::filesystem::path shaders() {
    return std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())} / "assets" / "shaders";
}
std::unique_ptr<ParticleWorld> make_world() {
    auto result = ParticleWorld::create(shaders());
    if (!result) throw std::runtime_error{result.error()};
    return std::move(*result);
}
void drain(ParticleWorld& world) {
    glFinish();
    world.poll();
}
void clear_samples(ParticleWorld& world) {
    static_cast<void>(world.take_cpu_timings());
    static_cast<void>(world.take_timings());
    static_cast<void>(world.take_step_profiles());
}
double percentile(std::vector<double> values, double q) {
    std::ranges::sort(values);
    return values[std::min(values.size() - 1, static_cast<std::size_t>(q * static_cast<double>(values.size())))];
}
} // namespace

std::expected<void, std::string> validate_readback(const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        std::ofstream report{output / "readback-tests.txt"};
        CallCounters counters;
        Api gl;
        gl.load();
        gl.observe(&counters);
        Producer producer{gl};
        report << "persistent_supported=" << gl.persistent_storage << '\n';
        for (const auto mode :
             {ReadbackMode::legacy, ReadbackMode::stream, ReadbackMode::staging, ReadbackMode::persistent}) {
            if (mode == ReadbackMode::persistent && !gl.persistent_storage) {
                report << "persistent=skipped (unsupported)\n";
                continue;
            }
            ReadbackRing ring{gl};
            ring.configure(mode);
            const auto slot_count = ring.stats().capacity;
            for (unsigned round = 0; round < 8; ++round) {
                for (unsigned i = 0; i < slot_count; ++i)
                    require(ring.submit(producer.write(static_cast<int>(round * 10 + i + 1)), 7, 1, i + 1),
                            "Free readback slot rejected");
                const auto calls_before = counters.api_calls;
                require(!ring.submit(0, 7, 1, 100), "Full ring overwrote pending data");
                require(counters.api_calls == calls_before, "Full ring issued GL work instead of skipping sample");
                require(ring.stats().pending == slot_count && ring.stats().max_pending == slot_count,
                        "Readback depth accounting failed");
                glFinish();
                const auto packet = ring.poll(7, 1, 10);
                require(packet.has_value(), "Completed readback missing");
                Producer::check(*packet, round * 10 + slot_count);
                require(ring.stats().pending == 0, "Completed slots not released");
            }
            require(ring.stats().dropped == 8 && ring.stats().completed == 8 * slot_count,
                    "Readback completion/drop accounting failed");
            // Advance the head by one, not only by a whole ring, to exercise
            // reuse of each physical slot with partially filled FIFO cycles.
            for (unsigned i = 0; i < 8; ++i) {
                require(ring.submit(producer.write(static_cast<int>(400 + i)), 7, 1, 30 + i),
                        "Partial ring wrap submit failed");
                glFinish();
                const auto packet = ring.poll(7, 1, 31 + i);
                require(packet.has_value(), "Partial ring wrap lost a packet");
                Producer::check(*packet, 400 + i);
            }
            for (const bool generation_change : {true, false}) {
                require(ring.submit(producer.write(99), 7, 1, 20), "Stale packet setup failed");
                glFinish();
                const auto reads = counters.read_bytes + counters.mapped_read_bytes;
                require(!ring.poll(generation_change ? 8U : 7U, generation_change ? 1U : 2U, 0),
                        "Stale generation/edit was accepted");
                require(counters.read_bytes + counters.mapped_read_bytes == reads,
                        "Discarded packet was needlessly read");
            }
            require(ring.stats().stale == 2, "Stale result accounting failed");
            require(ring.submit(producer.write(123), 8, 2, 21), "Released slot cannot be reused");
            glFinish();
            const auto fresh = ring.poll(8, 2, 23);
            require(fresh.has_value(), "Fresh generation not received");
            Producer::check(*fresh, 123);
            require(ring.stats().last_latency_ticks == 2, "Readback age accounting failed");
            require(ring.submit(producer.write(200), 8, 2, 24), "Pending reconfigure setup failed");
            ring.configure(mode == ReadbackMode::staging ? ReadbackMode::stream : ReadbackMode::staging);
            require(ring.submit(producer.write(201), 9, 3, 1), "Reconfigured readback queue unusable");
            glFinish();
            const auto changed = ring.poll(9, 3, 1);
            require(changed.has_value(), "Reconfigured queue lost fresh result");
            Producer::check(*changed, 201);
            report << readback_mode_name(mode)
                   << "=passed (GPU payload, full ring, wraps, stale generations/edits, pending reconfigure)\n";
        }
        {
            const bool supported = gl.persistent_storage;
            gl.persistent_storage = false;
            ReadbackRing ring{gl};
            ring.configure(ReadbackMode::automatic);
            require(ring.stats().mode == ReadbackMode::staging && ring.stats().capacity == 3,
                    "Automatic GL 4.3 fallback failed");
            bool rejected = false;
            try {
                ring.configure(ReadbackMode::persistent);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Unsupported persistent path was used");
            gl.persistent_storage = supported;
            report << "capability_fallback=passed (forced capability-off)\n";
        }
        {
            ReadbackRing ring{gl};
            ring.configure(ReadbackMode::automatic);
            require(ring.submit(producer.write(300), 1, 1, 1), "Pending destruction setup failed");
        }
        glFinish();
        require(glGetError() == GL_NO_ERROR, "GL error in readback transport/destruction");
        {
            auto world = make_world();
            ProfileOptions options;
            options.collect = true;
            options.readback_period = 1;
            world->set_profile_options(options);
            world->reset_mission();
            world->step();
            world->step();
            require(world->readback_stats().capacity == 3, "Production world did not select a three-slot ring");
            const auto generation = world->mission().status[1];
            world->reset_mission();
            drain(*world);
            require(world->mission().status[1] > generation && world->mission().status[2] == 0,
                    "Reset published an old HUD");
            world->step();
            world->reset(Scene::empty);
            drain(*world);
            require(world->metrics().grains == 0, "Old counters resurrected after scene change");
            world->reset_mission();
            world->step();
            world->paint({84, 120, 130, 155, 16, Material::erase});
            drain(*world);
            drain(*world);
            const auto physical = world->readback();
            unsigned actual = 0;
            for (const auto& p : physical) actual += p.motion[3] > .5F;
            require(world->metrics().grains == actual, "Edited scene accepted stale material counters");
            report << "world_reset_scene_edit=passed\n";
        }
        require(glGetError() == GL_NO_ERROR, "GL error in world readback regression");
        report << "status=passed\n";
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "readback-error.txt"} << e.what();
        return std::unexpected{e.what()};
    }
}

std::expected<void, std::string> profile_readback(const std::filesystem::path& output) {
    try {
        std::filesystem::create_directories(output);
        struct Variant {
            const char* name;
            ReadbackMode mode;
            unsigned period;
        };
        std::vector<Variant> variants{{"legacy", ReadbackMode::legacy, 1},
                                      {"stream", ReadbackMode::stream, 1},
                                      {"staging_1", ReadbackMode::staging, 1},
                                      {"staging_2", ReadbackMode::staging, 2}};
        Api capabilities;
        capabilities.load();
        if (capabilities.persistent_storage) {
            variants.push_back({"persistent_1", ReadbackMode::persistent, 1});
            variants.push_back({"persistent_2", ReadbackMode::persistent, 2});
        }
        variants.push_back({"off", ReadbackMode::automatic, 0});
        std::ofstream environment{output / "environment.txt"};
        environment
                << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
                << "\nversion=" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
                << "\npersistent_supported=" << capabilities.persistent_storage
                << "\nrepeats=3\nwarmup_ticks=120\nmeasured_ticks=240\nfresh_world_per_variant=1\nring_hud_poll=external/frame only\nlegacy_hud_poll=per tick and external\nmode=unpaced; drain every 16 ticks outside step timer\n";
        std::ofstream summary{output / "summary.csv"}, transport{output / "transport.csv"}, steps{output / "steps.csv"};
        summary << "variant,repeat,metric,samples,median,p95,p99,max\n";
        transport
                << "variant,repeat,mode,period,capacity,submitted,completed,dropped,stale,max_pending,pending,latency_ticks,hud_age_ticks,copy_bytes,read_bytes,mapped_read_bytes,fence_polls,pending_before\n";
        steps << "variant,repeat,tick,total_ms,poll_ms,fence_poll_ms,read_ms,solver_ms,staging_ms,hud_age_ticks,stage_copies,stage_drops,read_bytes,mapped_read_bytes\n";
        const auto metric = [&](const char* variant, unsigned repeat, const char* label,
                                const std::vector<double>& values) {
            if (!values.empty())
                summary << variant << ',' << repeat << ',' << label << ',' << values.size() << ','
                        << percentile(values, .5) << ',' << percentile(values, .95) << ',' << percentile(values, .99)
                        << ',' << *std::ranges::max_element(values) << '\n';
        };
        for (unsigned repeat = 1; repeat <= 3; ++repeat)
            for (std::size_t n = 0; n < variants.size(); ++n) {
                const auto& variant = variants[repeat % 2 == 0 ? variants.size() - 1 - n : n];
                auto world = make_world();
                ProfileOptions options;
                options.collect = true;
                options.readback_mode = variant.mode;
                options.readback_period = variant.period;
                world->set_profile_options(options);
                world->reset_mission();
                std::vector<double> external_polls;
                const auto collect = [&]() {
                    glFinish();
                    const auto began = SDL_GetTicksNS();
                    world->poll();
                    external_polls.push_back(static_cast<double>(SDL_GetTicksNS() - began) / 1e6);
                };
                const auto run = [&](unsigned count) {
                    for (unsigned i = 0; i < count; ++i) {
                        world->step();
                        if (i % 16 == 15) collect();
                    }
                    collect();
                };
                run(120);
                clear_samples(*world);
                external_polls.clear();
                const auto before = world->readback_stats();
                const auto initial = world->counters();
                const auto started = SDL_GetTicksNS();
                run(240);
                metric(variant.name, repeat, "batch_mean_step_wall_ms",
                       {static_cast<double>(SDL_GetTicksNS() - started) / 1e6 / 240});
                const auto samples = world->take_step_profiles();
                metric(variant.name, repeat, "cpu_submit_ms", world->take_cpu_timings());
                metric(variant.name, repeat, "gpu_step_ms", world->take_timings());
                metric(variant.name, repeat, "external_poll_ms", external_polls);
                for (const auto& [name, member] : {std::pair{"poll_ms", &StepProfile::poll_ms},
                                                   std::pair{"fence_poll_ms", &StepProfile::fence_poll_ms},
                                                   std::pair{"read_ms", &StepProfile::read_ms},
                                                   std::pair{"solver_submit_ms", &StepProfile::solver_ms},
                                                   std::pair{"staging_submit_ms", &StepProfile::staging_ms}}) {
                    std::vector<double> values;
                    for (const auto& p : samples) values.push_back(p.*member);
                    metric(variant.name, repeat, name, values);
                }
                const auto after = world->readback_stats();
                const auto calls = world->counters() - initial;
                transport << variant.name << ',' << repeat << ',' << readback_mode_name(after.mode) << ','
                          << variant.period << ',' << after.capacity << ',' << after.submitted - before.submitted << ','
                          << after.completed - before.completed << ',' << after.dropped - before.dropped << ','
                          << after.stale - before.stale << ',' << after.max_pending << ',' << after.pending << ','
                          << after.last_latency_ticks << ',' << world->metrics().steps - world->mission().status[2]
                          << ',' << calls.copy_bytes << ',' << calls.read_bytes << ',' << calls.mapped_read_bytes << ','
                          << calls.fence_polls << ',' << before.pending << '\n';
                for (const auto& p : samples)
                    steps << variant.name << ',' << repeat << ',' << p.tick << ',' << p.total_ms << ',' << p.poll_ms
                          << ',' << p.fence_poll_ms << ',' << p.read_ms << ',' << p.solver_ms << ',' << p.staging_ms
                          << ',' << p.readback_age_ticks << ',' << p.calls.stage_copies << ',' << p.calls.stage_drops
                          << ',' << p.calls.read_bytes << ',' << p.calls.mapped_read_bytes << '\n';
                summary.flush();
                transport.flush();
                steps.flush();
            }
        require(glGetError() == GL_NO_ERROR, "GL error during readback benchmark");
        return {};
    } catch (const std::exception& e) {
        std::ofstream{output / "readback-error.txt"} << e.what();
        return std::unexpected{e.what()};
    }
}
} // namespace strefa::gpu
