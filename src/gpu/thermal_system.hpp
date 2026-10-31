#pragma once
#include "gpu/thermal.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/gl_state.hpp"
#include <span>
namespace strefa::gpu::detail {
class ThermalSystem {
public:
    ThermalSystem(Api& g,GlStateTracker& s):gl{g},state{s}{}
    ~ThermalSystem();
    void initialize(ShaderLibrary&,const std::filesystem::path&,const std::string&);
    void reset(const std::array<GLuint,8>&,unsigned current,unsigned generation);
    void synchronize(const std::array<GLuint,8>&,unsigned current);
    void tick(const std::array<GLuint,8>&,unsigned& current,unsigned tick,const ThermalBrush&);
    void consume_events(const std::array<GLuint,8>&,unsigned generation,unsigned tick);
    void condense(const std::array<GLuint,8>&,unsigned& current);
    void erase(const std::array<GLuint,8>&,unsigned current,const ThermalBrush&);
    void draw(GLuint target,int width,int height,bool linear=false);
    void presentation(GLuint target);
    GLuint presentation_particles()const{return particles[particle_bank];}
    void request_inspection(int x,int y);
    const ThermalInspection& inspection()const{return inspected;}
    void poll();
    const ThermalTotals& totals()const{return latest;}
    ThermalOptions options;ThermalView view{};
    ThermalSnapshot snapshot();
    void seed(std::span<const ThermalCell>,std::span<const ThermalParticle>);
    std::vector<ThermalTiming> take_timings();
    std::uint64_t memory_bytes()const;
private:
    Api& gl;GlStateTracker& state;GLuint presentation_program{};GLint linear_location{};GLuint graphics{},condensation{},events{},reduce_program{},vao{};
    std::array<GLuint,2> particles{},cells{};GLuint auxiliary{},reduction{};
    unsigned particle_bank{},cell_bank{},generation{},tick_{},serial{},command_id{};
    struct Kernel {GLuint program{};GLint dt{-1},segment{-1},radius{-1},dose{-1},tool{-1},pressure{-1},radiation{-1},generation{-1},tick{-1},command{-1};ThermalBrush previous;bool brush_known{};int previous_radiation{-1},previous_pressure{-1};unsigned previous_generation{~0u},previous_tick{~0u},previous_command{~0u};};
    std::array<Kernel,24> kernels;ThermalBrush pending_brush;unsigned pressure_bank{};
    GLint view_location{},extent_location{},event_mode{},event_generation{},event_tick{};
    std::array<GLuint,8> physics{};unsigned physical_bank{};
    struct Probe {GLuint buffer{};GLsync fence{};unsigned serial{},generation{},tick{};std::array<GLuint,5> queries{};bool measured{};};
    std::array<Probe,8> probes{};unsigned head{},count{};Probe* active{};
    struct InspectProbe {GLuint buffer{};GLsync fence{};unsigned serial{},generation{},tick{};int x{},y{};};
    std::array<InspectProbe,3> inspections{};unsigned inspect_head{},inspect_count{};ThermalInspection inspected;
    ThermalTotals latest;std::vector<ThermalTiming> timings;
    void bind();void run(int,unsigned);void swap_cells(){cell_bank^=1;}void swap_particles(unsigned& current){particle_bank^=1;current^=1;physical_bank=current;}
    void brush(const ThermalBrush&);void capture(bool force=false);ThermalTotals read_totals(GLuint)const;
};
}
