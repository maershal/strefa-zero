#pragma once
#include "gpu/shader_library.hpp"
#include "gpu/gl_state.hpp"
#include "gpu/render_options.hpp"
#include "gpu/lighting_system.hpp"
#include "gpu/vfx_pool.hpp"
#include "gpu/thermal_system.hpp"
#include <array>
#include <unordered_map>
#include <utility>
namespace strefa::gpu::detail {
struct RenderState {std::array<GLuint,8> buffers{};unsigned current{};bool fluids_enabled{},mission_enabled{};};
struct RenderPassDescription {std::string_view name,inputs,output,format,state,barrier;};
inline constexpr std::array<RenderPassDescription,11> scene_passes{{
    {"presentation_tick","physical particle IDs,water,thermal,confirmed events","Moisture + lights","R32F + SSBO","compute; once per tick","IMAGE_ACCESS + SHADER_STORAGE"},
    {"scene","background,terrain,particles,actors,submerged VFX","SceneColor + Receiver","RGBA16F + R8UI","opaque; actors alpha blend","SSBO before draw"},
    {"direct_light","SceneColor,terrain,light list,JFA high","LitScene","RGBA16F","compute visibility; opaque apply","TEXTURE_FETCH after imageStore"},
    {"splat","particles,terrain","FluidRaw","RGBA16F: Dw,Do,sum(w*vx),sum(w*vy)","additive ONE/ONE","ordered framebuffer writes"},
    {"filter_x","FluidRaw,terrain","FluidTemporary","RGBA16F","opaque","separate source and destination"},
    {"filter_y","FluidTemporary,FluidRaw,terrain","FluidSmooth","RGBA16F","opaque","separate source and destination"},
    {"thickness","FluidRaw,terrain","Thickness","R16F; segmented cells","compute image write","TEXTURE_FETCH after imageStore"},
    {"composition","SceneColor,Receiver,FluidRaw,FluidSmooth,Thickness,terrain,post VFX,distortion copy","HDRWorld / World","RGBA16F linear / RGBA8 display","opaque","separate source and destination"},
    {"bloom","HDRWorld","Bloom","RGBA16F down/up chain","separate source/destination","ordered framebuffer writes"},
    {"postprocess","HDRWorld,Bloom,LUT,camera","World","RGBA8 sRGB encoded","opaque; manual single encoding","before sharp overlays"},
    {"foreground","confirmed targets,exit,flash","World","RGBA8 display","SRC_ALPHA/ONE_MINUS_SRC_ALPHA","framebuffer 0 before UI"}
}};
class SceneRenderer {
public:
    SceneRenderer(Api& api,GlStateTracker& tracker):lighting{api,tracker},gl{api},state{tracker}{}
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&)=delete;SceneRenderer& operator=(const SceneRenderer&)=delete;
    void initialize(ShaderLibrary&,const std::filesystem::path&,const std::string& defines);
    LightingSystem lighting;
    VfxPool* vfx{};ThermalSystem* thermal{};
    void build_vfx_field(const RenderState&);
    void render(const RenderState&,float interpolation);
    void save_image(const std::filesystem::path&) const;
    void set_options(RenderOptions value){if(!value.lighting.valid())throw std::invalid_argument{"Invalid lighting options"};options=value;lighting.set_profile(value.profile_passes);}
    RenderOptions get_options()const{return options;}
    std::vector<RenderSample> take_samples();
    void poll_samples();
    void begin_terrain_update();void end_terrain_update();
    std::vector<double> take_terrain_samples(){poll_samples();return std::exchange(terrain_samples,{});}

    void resize_for_test(int scale);
    std::vector<std::uint16_t> read_terrain_texture()const;
    std::vector<float> read_field(unsigned which=0) const;
    std::vector<std::uint8_t> read_image() const;
    std::uint64_t memory_bytes()const;
    void bind_probe_field(){gl.ActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,raw_image);gl.ActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,terrain_image);}
    RenderState freeze(const RenderState&);
    void release_frozen();
    void reset_profiles();
    int render_width()const{return rw;}int render_height()const{return rh;}
    GLuint image{},terrain_image{};
    unsigned parts_texture{},weapons_texture{},background_texture{};int weapon_width{256},weapon_height{128};
    TargetPresentation targets;
    RenderStage stage{RenderStage::complete};bool bilateral{true};
private:
    std::unordered_map<GLuint,std::unordered_map<std::string,GLint>> uniforms;
    Api& gl;GlStateTracker& state;RenderOptions options;
    int rw{960},rh{576};
    GLuint field_prepare{},field_command{},field_splat{},vao{},world_fbo{},scene_fbo{},field_fbo{},scene_image{},receiver_image{},raw_image{},temporary_image{},smooth_image{},thickness_image{};
    GLuint hdr_scene{},hdr_scene_fbo{},hdr_world{},hdr_world_fbo{};
    GLuint grains{},actors{},background{},terrain{},splat{},filter{},thickness{},composition{},reference_splat{},reference_surface{};
    std::array<GLuint,8> frozen_buffers{};
    struct Timing {std::array<GLuint,10> ids{};bool pending{};std::uint64_t frame{};};
    std::array<Timing,32> timings{};unsigned timing_head{},timing_cursor{};std::uint64_t frame{};
    struct TerrainTiming {std::array<GLuint,2> ids{};bool pending{};};
    std::array<TerrainTiming,32> terrain_timings{};unsigned terrain_head{},terrain_cursor{};bool terrain_active{};
    std::vector<double> terrain_samples;
    std::vector<RenderSample> samples;
    void allocate_targets();void delete_targets();
    void attachment(GLuint texture);void extent(GLuint program,const char* uniform);
    void actor_pass(const RenderState&,float,int offset,int count,bool overlay,bool reference=false);
    void draw_active(const RenderState&);void fullscreen(GLuint);
    GLint location(GLuint p,const char* name)const;
};
}
