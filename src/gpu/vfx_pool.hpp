#pragma once
#include "gpu/gl_state.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/vfx.hpp"

#include <span>
namespace strefa::gpu::detail {
class VfxPool {
public:
    VfxPool(Api& api, GlStateTracker& tracker) : gl{api}, state{tracker} {}
    ~VfxPool();
    void initialize(ShaderLibrary&, const std::filesystem::path&);
    void set_options(VfxOptions);
    VfxOptions options() const { return config; }
    void reset(unsigned generation = 0);
    bool enabled() const { return initialized && config.quality != VfxQuality::off; }
    // Field generation is owned by the source adapter; these textures never alias physics.
    void begin_tick(GLuint terrain, unsigned generation, unsigned tick, float dt);
    void field_target();
    void upload_field(std::span<const float>, std::span<const std::uint16_t>);
    void sand_sources(GLuint source) { sand_field = source; }
    void end_tick(GLuint mission_buffer = 0, std::span<const VfxEvent> events = {}, bool water = true);
    void draw(GLuint target, int width, int height, int layer, float interpolation, bool linear = false);
    void distort(GLuint source, GLuint target, int width, int height, float interpolation, bool linear = false);
    void cpu_canvas(GLuint canvas, GLuint background, float interpolation);
    GLuint cpu_back() const { return cpu_images[0]; }
    GLuint cpu_front() const { return cpu_images[1]; }
    const VfxCounters& stats() const { return latest; }
    std::vector<VfxSample> take_samples();
    void poll();
    std::uint64_t memory_bytes() const;
    unsigned timing_query_objects() const {
        unsigned n = 0;
        for (const auto& p : probes)
            if (p.queries[0]) n += 4;
        return n;
    }
    VfxSnapshot snapshot(); // explicit diagnostics only
    void seed_for_test(std::span<const VfxParticle>);
    GLuint field_texture() const { return field; }

private:
    Api& gl;
    GlStateTracker& state;
    VfxOptions config;
    bool initialized{};
    std::array<GLuint, 7> buffers{};
    unsigned bank{}, generation{}, tick{};
    float dt{}, normalization{1};
    GLuint sand_field{};
    GLuint field{}, terrain{}, cpu_terrain{}, fbo{}, vao{}, scratch{}, scratch_fbo{};
    std::array<GLuint, 2> cpu_images{}, cpu_fbos{};
    int scratch_width{}, scratch_height{};
    bool scratch_linear{};
    GLuint init{}, prepare{}, update{}, events_program{}, water_program{}, finish{}, graphics{}, copy{},
            canvas_program{};
    struct ProgramUniforms {
        GLuint id{};
        std::array<GLint, 13> locations{};
        float last_dt{-1}, last_normalization{-1};
        int last_quality{-1};
        unsigned last_generation{~0u}, last_tick{~0u};
    };
    std::vector<ProgramUniforms> uniforms;
    GLint location(GLuint, const char*) const;
    static constexpr std::array<const char*, 13> uniform_names{
            "stepData",         "quality",      "cpuSource",         "layer",       "alpha",
            "linearOutput",     "renderExtent", "copyMode",          "sourceImage", "backgroundImage",
            "sourceGeneration", "sourceTick",   "fieldNormalization"};
    VfxCounters latest{};
    struct Probe {
        GLuint buffer{};
        GLsync fence{};
        std::array<GLuint, 4> queries{};
        std::uint64_t tick{};
        unsigned serial{};
        bool draw{}, measured{};
    };
    std::array<Probe, 128> probes{};
    unsigned probe_head{}, probe_count{}, serial{};
    Probe* active_probe{};
    std::vector<VfxSample> samples;
    void bind();
    void run(GLuint, unsigned groups = 1);
    void allocate_scratch(int, int, bool);
    Probe* begin_probe(bool draw, bool metadata = false);
    void close_probe(Probe*);
};
} // namespace strefa::gpu::detail
