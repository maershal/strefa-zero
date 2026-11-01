#pragma once
#include "gpu/gl_state.hpp"
#include "gpu/lighting.hpp"
#include "gpu/shader_library.hpp"

#include <span>
#include <unordered_map>
namespace strefa::gpu::detail {
class LightingSystem {
public:
    LightingSystem(Api& g, GlStateTracker& s) : gl{g}, state{s} {}
    ~LightingSystem();
    void initialize(ShaderLibrary&, const std::filesystem::path&, const std::string&);
    void reset(unsigned generation);
    void bind_birth();
    void prepare_birth(const std::array<GLuint, 8>&, unsigned current, bool conditional = false);
    void tick(const std::array<GLuint, 8>&, unsigned current, unsigned generation, unsigned tick, bool mission,
              GLuint terrain, GLuint heat = 0, GLuint thermal_particles = 0);
    void illuminate(GLuint scene, GLuint receiver, GLuint terrain, int width, int height, GLuint fbo,
                    const LightingOptions&);
    void finish(GLuint hdr, GLuint target, int width, int height, const LightingOptions&);
    void set_lamps(std::span<const PointLight>);
    std::array<float, 2> camera(const LightingOptions&) const;
    std::vector<LightingSample> take_samples();
    void set_profile(bool value) { profile = value; }
    std::uint64_t memory_bytes() const;
    std::vector<float> read_moisture() const;
    std::vector<float> read_light() const;
    std::vector<std::uint16_t> read_seeds() const;
    LightingSnapshot snapshot() const;
    float impulse{};
    GLuint moisture{}, field{}, density{}, light_image{}, thermal_image{};
    void resize(int, int);

private:
    Api& gl;
    GlStateTracker& state;
    GLuint birth_program{}, candidates{}, material_program{};
    GLuint vao{}, fbo{}, pool{}, lamps{}, field_command{}, field_program{}, field_vertex{}, moisture_program{},
            event_program{}, shadow_program{}, jfa_program{}, light_program{}, apply_program{}, bloom_program{},
            post_program{};
    GLuint shadow_mask{}, seeds[2]{}, post_scratch{};
    std::array<GLuint, 3> bloom_down{}, bloom_up{};
    unsigned generation{}, last_tick{}, light_count{}, lamp_count{3};
    bool tick_known{}, profile{}, jfa_valid{};
    int rw{}, rh{};
    std::uint64_t serial{};
    std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> uniforms;
    struct Probe {
        std::array<GLuint, 8> queries{};
        bool pending{}, tick{};
        std::uint64_t serial{};
    };
    std::array<Probe, 128> probes{};
    unsigned head{}, count{};
    Probe* active{};
    std::vector<LightingSample> samples;
    GLint location(GLuint, const char*) const;
    void attach(GLuint, int, int);
    void image(unsigned, GLuint, GLenum, GLenum);
    void begin(bool);
    void mark(unsigned);
    void end();
    void poll();
};
} // namespace strefa::gpu::detail
