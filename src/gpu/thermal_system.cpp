#include "gpu/thermal_system.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <bit>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>
namespace strefa::gpu::detail {
namespace {
constexpr unsigned cell_count=320*192,particle_count=32768,block_count=240;
constexpr std::size_t totals_bytes=block_count*16*sizeof(double);
std::string material_defines(){
    static_assert(sim::physical.size()==20,"Review thermal shader table dimensions when adding materials");
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out<<"const vec4 thermalProperties[20]=vec4[20](";
    for(unsigned i=0;i<sim::physical.size();++i){const auto& p=sim::physical[i];if(i)out<<',';out<<"vec4("<<static_cast<float>(p.density)<<','<<static_cast<float>(p.heat_capacity)<<','<<static_cast<float>(p.conductance)<<','<<static_cast<float>(sim::enthalpy_offset(static_cast<sim::Material>(i)))<<')';}out<<");\nconst vec4 combustionProperties[20]=vec4[20](";
    for(unsigned i=0;i<sim::physical.size();++i){const auto& p=sim::physical[i];if(i)out<<',';out<<"vec4("<<static_cast<float>(p.ignition)<<','<<static_cast<float>(p.burn_rate)<<','<<static_cast<float>(p.heat_release)<<','<<static_cast<float>(p.residue)<<')';}out<<");\nconst float spawnTemperatures[20]=float[20](";
    for(unsigned i=0;i<sim::physical.size();++i){if(i)out<<',';out<<static_cast<float>(sim::physical[i].spawn_temperature);}out<<");\n";return out.str();
}
}
ThermalSystem::~ThermalSystem(){
    for(auto& p:probes){if(p.fence)gl.DeleteSync(p.fence);if(p.buffer)gl.DeleteBuffers(1,&p.buffer);if(p.queries[0])gl.DeleteQueries(5,p.queries.data());}
    for(auto& p:inspections){if(p.fence)gl.DeleteSync(p.fence);if(p.buffer)gl.DeleteBuffers(1,&p.buffer);}
    gl.DeleteBuffers(2,particles.data());gl.DeleteBuffers(2,cells.data());if(auxiliary)gl.DeleteBuffers(1,&auxiliary);if(reduction)gl.DeleteBuffers(1,&reduction);if(vao)gl.DeleteVertexArrays(1,&vao);
}
void ThermalSystem::initialize(ShaderLibrary& library,const std::filesystem::path& folder,const std::string& defines){
    const auto config=defines+material_defines();
    for(unsigned op=0;op<kernels.size();++op){if(op==22)continue;auto& k=kernels[op];k.program=library.program({{GL_COMPUTE_SHADER,folder/"thermal.comp"}},config+"\n#define THERMAL_OPERATION "+std::to_string(op)+"\n");
        k.dt=gl.GetUniformLocation(k.program,"timeStep");k.segment=gl.GetUniformLocation(k.program,"brushSegment");k.radius=gl.GetUniformLocation(k.program,"brushRadius");k.dose=gl.GetUniformLocation(k.program,"dose");k.tool=gl.GetUniformLocation(k.program,"tool");k.pressure=gl.GetUniformLocation(k.program,"pressureBank");k.radiation=gl.GetUniformLocation(k.program,"radiationEnabled");k.generation=gl.GetUniformLocation(k.program,"sourceGeneration");k.tick=gl.GetUniformLocation(k.program,"sourceTick");k.command=gl.GetUniformLocation(k.program,"sourceCommand");state.use(k.program);if(k.dt>=0)gl.Uniform1f(k.dt,1.F/120.F);
    }
    events=library.program({{GL_COMPUTE_SHADER,folder/"thermal_events.comp"}},config);
    condensation=library.program({{GL_COMPUTE_SHADER,folder/"thermal_condense.comp"}},config);
    // A distinct program owns the double-precision diagnostic reduction, outside the solver.
    reduction=0;reduce_program=library.program({{GL_COMPUTE_SHADER,folder/"thermal_reduce.comp"}},material_defines());
    graphics=library.program({{GL_VERTEX_SHADER,folder/"liquid_surface.vert"},{GL_FRAGMENT_SHADER,folder/"thermal_draw.frag"}},material_defines());
    presentation_program=library.program({{GL_COMPUTE_SHADER,folder/"lighting_thermal.comp"}},material_defines());linear_location=gl.GetUniformLocation(graphics,"linearOutput");view_location=gl.GetUniformLocation(graphics,"viewMode");extent_location=gl.GetUniformLocation(graphics,"renderExtent");event_mode=gl.GetUniformLocation(events,"mode");event_generation=gl.GetUniformLocation(events,"sourceGeneration");event_tick=gl.GetUniformLocation(events,"sourceTick");
    gl.GenBuffers(2,particles.data());gl.GenBuffers(2,cells.data());gl.GenBuffers(1,&auxiliary);gl.GenBuffers(1,&reduction);gl.GenVertexArrays(1,&vao);
    const auto allocate=[&](GLuint b,std::size_t bytes){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b);gl.BufferData(GL_SHADER_STORAGE_BUFFER,static_cast<GLsizeiptr>(bytes),nullptr,GL_DYNAMIC_COPY);};
    for(auto b:particles)allocate(b,particle_count*sizeof(ThermalParticle));
    for(auto b:cells)allocate(b,cell_count*sizeof(ThermalCell));
    allocate(auxiliary,(4*cell_count+particle_count)*sizeof(std::uint32_t)+32);allocate(reduction,totals_bytes);state.invalidate();
}
void ThermalSystem::bind(){state.base(0,physics[physical_bank]);state.base(1,physics[physical_bank^1]);state.base(2,particles[particle_bank]);state.base(3,particles[particle_bank^1]);state.base(4,physics[4]);state.base(5,cells[cell_bank]);state.base(6,cells[cell_bank^1]);state.base(7,auxiliary);}
void ThermalSystem::run(int op,unsigned n){
    bind();auto& k=kernels[static_cast<unsigned>(op)];state.use(k.program);const bool first=!k.brush_known;
    if(!k.brush_known||k.previous!=pending_brush){const auto& p=pending_brush;if(k.segment>=0)gl.Uniform4f(k.segment,p.segment[0],p.segment[1],p.segment[2],p.segment[3]);if(k.radius>=0)gl.Uniform1f(k.radius,p.radius);if(k.dose>=0)gl.Uniform1f(k.dose,p.rate/120.F);if(k.tool>=0)gl.Uniform1i(k.tool,static_cast<int>(p.tool));k.previous=p;k.brush_known=true;}
    if(k.pressure>=0&&(first||k.previous_pressure!=static_cast<int>(pressure_bank))){gl.Uniform1i(k.pressure,static_cast<int>(pressure_bank));k.previous_pressure=static_cast<int>(pressure_bank);}
    if(k.radiation>=0&&(first||k.previous_radiation!=static_cast<int>(options.radiation))){gl.Uniform1i(k.radiation,options.radiation?1:0);k.previous_radiation=static_cast<int>(options.radiation);}
    if(k.generation>=0&&(first||k.previous_generation!=generation)){gl.Uniform1i(k.generation,std::bit_cast<int>(generation));k.previous_generation=generation;}
    if(k.tick>=0&&(first||k.previous_tick!=tick_)){gl.Uniform1i(k.tick,std::bit_cast<int>(tick_));k.previous_tick=tick_;}
    if(k.command>=0&&(first||k.previous_command!=command_id)){gl.Uniform1i(k.command,std::bit_cast<int>(command_id));k.previous_command=command_id;}
    gl.DispatchCompute((n+127)/128,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
void ThermalSystem::brush(const ThermalBrush& b){pending_brush=b;}
void ThermalSystem::reset(const std::array<GLuint,8>& buffers,unsigned current,unsigned gen){
    physics=buffers;physical_bank=current;generation=gen;particle_bank=cell_bank=tick_=command_id=0;++serial;latest={};inspected={};latest.generation=gen;active=nullptr;
    const std::vector<ThermalParticle> zero(particle_count);for(auto b:particles){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(zero.size()*sizeof(ThermalParticle)),zero.data());}
    state.invalidate();brush({});run(0,cell_count);synchronize(buffers,current);state.invalidate();
}
void ThermalSystem::synchronize(const std::array<GLuint,8>& buffers,unsigned current){physics=buffers;physical_bank=current;brush({});run(2,cell_count);run(1,particle_count);run(3,cell_count);state.invalidate();}
void ThermalSystem::tick(const std::array<GLuint,8>& buffers,unsigned& current,unsigned tick,const ThermalBrush& b){
    if(tick<=tick_)return; // One completed solver tick advances this state once.
    const auto began=SDL_GetTicksNS();physics=buffers;physical_bank=current;tick_=tick;poll();active=nullptr;
    if((options.profile||tick%30==0)&&count<probes.size()){
        auto& p=probes[(head+count)%probes.size()];if(!p.buffer){gl.GenBuffers(1,&p.buffer);gl.BindBuffer(GL_COPY_WRITE_BUFFER,p.buffer);gl.BufferData(GL_COPY_WRITE_BUFFER,totals_bytes,nullptr,GL_STREAM_READ);}p.measured=options.profile;p.serial=serial;p.generation=generation;p.tick=tick;
        if(p.measured){if(!p.queries[0])gl.GenQueries(5,p.queries.data());gl.QueryCounter(p.queries[0],GL_TIMESTAMP);}active=&p;
    }
    brush(b);if(b.tool!=ThermalTool::none){if(command_id==std::numeric_limits<unsigned>::max())throw std::overflow_error{"Thermal source IDs exhausted"};++command_id;}run(23,1);run(2,cell_count);run(1,particle_count);run(3,cell_count);run(4,particle_count);run(5,cell_count);run(6,particle_count);run(7,cell_count);swap_cells();swap_particles(current);run(8,cell_count);swap_cells();
    if(active&&active->measured)gl.QueryCounter(active->queries[1],GL_TIMESTAMP);
    if(options.transport){run(9,cell_count);run(14,cell_count);for(unsigned k=0;k<options.pressure_iterations;++k){pressure_bank=k%2;run(10,cell_count);}pressure_bank=options.pressure_iterations%2;run(11,cell_count);run(12,cell_count);run(13,cell_count);swap_cells();}
    if(active&&active->measured)gl.QueryCounter(active->queries[2],GL_TIMESTAMP);
    if(options.reactions){run(15,particle_count);run(16,cell_count);run(17,particle_count);run(18,cell_count);swap_cells();swap_particles(current);run(19,cell_count);}
    if(active&&active->measured)gl.QueryCounter(active->queries[3],GL_TIMESTAMP);
    const double cpu=static_cast<double>(SDL_GetTicksNS()-began)/1e6;if(options.profile)timings.push_back({tick,0,0,0,0,cpu});capture();state.invalidate();
}
void ThermalSystem::consume_events(const std::array<GLuint,8>& buffers,unsigned gen,unsigned tick){
    state.base(4,buffers[4]);state.base(5,cells[cell_bank]);state.base(6,buffers[6]);state.base(7,auxiliary);state.use(events);gl.Uniform1i(event_generation,std::bit_cast<int>(gen));gl.Uniform1i(event_tick,std::bit_cast<int>(tick));gl.Uniform1i(event_mode,0);gl.DispatchCompute(1,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);gl.Uniform1i(event_mode,1);gl.DispatchCompute((cell_count+127)/128,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);state.invalidate();
}
void ThermalSystem::condense(const std::array<GLuint,8>& buffers,unsigned& current){
    if(!options.reactions)return;
    physics=buffers;physical_bank=current;run(21,particle_count);
    state.base(0,physics[current]);state.base(1,physics[current^1]);state.base(2,particles[particle_bank]);state.base(3,cells[cell_bank]);state.base(4,physics[4]);state.base(5,physics[5]);state.base(6,physics[6]);state.use(condensation);gl.DispatchCompute((cell_count+127)/128,1,1);gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);current^=1;physical_bank=current;state.invalidate();
}
void ThermalSystem::erase(const std::array<GLuint,8>& buffers,unsigned current,const ThermalBrush& region){
    synchronize(buffers,current);brush(region);run(20,particle_count);run(3,cell_count);state.invalidate();
}
void ThermalSystem::capture(bool force){
    if(!active&&!force)return;
    state.base(0,particles[particle_bank]);state.base(1,cells[cell_bank]);state.base(2,reduction);state.use(reduce_program);gl.DispatchCompute(2,1,1);gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    if(active){gl.BindBuffer(GL_COPY_READ_BUFFER,reduction);gl.BindBuffer(GL_COPY_WRITE_BUFFER,active->buffer);gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,totals_bytes);active->fence=gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);++count;active=nullptr;}state.invalidate();
}
ThermalTotals ThermalSystem::read_totals(GLuint buffer)const{
    std::vector<std::array<double,16>> blocks(block_count);gl.BindBuffer(GL_COPY_READ_BUFFER,buffer);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,totals_bytes,blocks.data());std::array<double,16> sums{};
    for(const auto& b:blocks)for(unsigned i=0;i<sums.size();++i){if(i==12)sums[i]=std::max(sums[i],b[i]);else sums[i]+=b[i];}
    ThermalTotals t;t.radiated=sums[15];t.mass=sums[0];t.enthalpy=sums[1];t.chemical=sums[2];t.external_mass=sums[3];t.external_enthalpy=sums[4];t.external_chemical=sums[5];t.released_heat=sums[6];t.oxygen=sums[7];t.fuel=sums[8];t.steam=sums[9];t.smoke=sums[10];t.divergence_l2=std::sqrt(sums[11]/cell_count);t.divergence_max=sums[12];t.finite=sums[13]==0&&std::ranges::all_of(sums,[](double x){return std::isfinite(x);});return t;
}
void ThermalSystem::request_inspection(int x,int y){
    if(x<0||y<0||x>=320||y>=192||inspect_count==inspections.size())return;
    auto& p=inspections[(inspect_head+inspect_count)%inspections.size()];
    if(!p.buffer){gl.GenBuffers(1,&p.buffer);gl.BindBuffer(GL_COPY_WRITE_BUFFER,p.buffer);gl.BufferData(GL_COPY_WRITE_BUFFER,sizeof(ThermalCell),nullptr,GL_STREAM_READ);}
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_COPY_READ_BUFFER,cells[cell_bank]);gl.BindBuffer(GL_COPY_WRITE_BUFFER,p.buffer);gl.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,static_cast<GLintptr>((y*320+x)*sizeof(ThermalCell)),0,sizeof(ThermalCell));p.fence=gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);p.serial=serial;p.generation=generation;p.tick=tick_;p.x=x;p.y=y;++inspect_count;state.invalidate();
}
void ThermalSystem::poll(){
    while(inspect_count){auto& p=inspections[inspect_head];const auto status=gl.ClientWaitSync(p.fence,0,0);if(status==GL_TIMEOUT_EXPIRED)break;if(status==GL_WAIT_FAILED)throw std::runtime_error{"Thermal inspector fence failed"};gl.DeleteSync(p.fence);p.fence=nullptr;if(p.serial==serial){gl.BindBuffer(GL_COPY_READ_BUFFER,p.buffer);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(ThermalCell),&inspected.value);inspected.cell={p.x,p.y};inspected.generation=p.generation;inspected.tick=p.tick;inspected.valid=true;}inspect_head=(inspect_head+1)%static_cast<unsigned>(inspections.size());--inspect_count;}
    while(count){auto& p=probes[head];const auto ready=gl.ClientWaitSync(p.fence,0,0);if(ready==GL_TIMEOUT_EXPIRED)break;if(ready==GL_WAIT_FAILED)throw std::runtime_error{"Thermal telemetry fence failed"};gl.DeleteSync(p.fence);p.fence=nullptr;
        if(p.serial==serial){latest=read_totals(p.buffer);latest.tick=p.tick;latest.generation=p.generation;if(p.measured){std::array<GLuint64,4> stamp{};for(unsigned i=0;i<4;++i)gl.GetQueryObjectui64v(p.queries[i],GL_QUERY_RESULT,&stamp[i]);ThermalTiming t;t.tick=p.tick;t.heat_ms=static_cast<double>(stamp[1]-stamp[0])/1e6;t.gas_ms=static_cast<double>(stamp[2]-stamp[1])/1e6;t.reaction_ms=static_cast<double>(stamp[3]-stamp[2])/1e6;timings.push_back(t);}}
        head=(head+1)%static_cast<unsigned>(probes.size());--count;
    }state.invalidate();
}
ThermalSnapshot ThermalSystem::snapshot(){capture(true);glFinish();poll();ThermalSnapshot s;s.cells.resize(cell_count);s.particles.resize(particle_count);gl.BindBuffer(GL_COPY_READ_BUFFER,cells[cell_bank]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(s.cells.size()*sizeof(ThermalCell)),s.cells.data());gl.BindBuffer(GL_COPY_READ_BUFFER,particles[particle_bank]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,static_cast<GLsizeiptr>(s.particles.size()*sizeof(ThermalParticle)),s.particles.data());s.totals=read_totals(reduction);s.totals.tick=tick_;s.totals.generation=generation;state.invalidate();return s;}
void ThermalSystem::seed(std::span<const ThermalCell> c,std::span<const ThermalParticle> p){if(c.size()!=cell_count||p.size()!=particle_count)throw std::logic_error{"Invalid thermal fixture size"};++serial;for(auto b:cells){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(c.size_bytes()),c.data());}for(auto b:particles){gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b);gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(p.size_bytes()),p.data());}state.invalidate();}
void ThermalSystem::presentation(GLuint target){PresentationCounterScope observation{gl};state.invalidate();state.base(5,cells[cell_bank]);state.use(presentation_program);gl.BindImageTexture(0,target,0,static_cast<GLboolean>(GL_FALSE),0,GL_WRITE_ONLY,GL_RGBA16F);gl.DispatchCompute((cell_count+127)/128,1,1);gl.MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);state.invalidate();}
void ThermalSystem::draw(GLuint target,int w,int h,bool linear){state.invalidate();state.raster(w,h);state.framebuffer(target);state.vertex_array(vao);state.base(4,physics[4]);state.base(5,cells[cell_bank]);state.use(graphics);gl.Uniform1i(linear_location,linear?1:0);gl.Uniform1i(view_location,static_cast<int>(view));gl.Uniform4f(extent_location,static_cast<float>(w),static_cast<float>(h),0.F,0.F);glEnable(GL_BLEND);gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLES,0,3);glDisable(GL_BLEND);state.invalidate();}
std::vector<ThermalTiming> ThermalSystem::take_timings(){poll();return std::exchange(timings,{});}
std::uint64_t ThermalSystem::memory_bytes()const{std::uint64_t n=2*particle_count*sizeof(ThermalParticle)+2*cell_count*sizeof(ThermalCell)+(4*cell_count+particle_count)*4+32+totals_bytes;for(const auto& p:probes)if(p.buffer)n+=totals_bytes;for(const auto& p:inspections)if(p.buffer)n+=sizeof(ThermalCell);return n;}
}
