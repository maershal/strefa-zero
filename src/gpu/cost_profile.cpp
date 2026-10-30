#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace strefa::gpu {
namespace {
constexpr unsigned cells_count=width*height,start_base=16+cells_count,offset_base=16+3*cells_count+240;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error{message};}
void drain(ParticleWorld& world){glFinish();world.poll();}
double percentile(std::vector<double> values,double q){
    require(!values.empty(),"Missing diagnostic samples");std::ranges::sort(values);
    return values[std::min(values.size()-1,static_cast<std::size_t>(q*static_cast<double>(values.size())))];
}
std::vector<Grain> unpack(const SpatialCapture& capture){
    require(capture.particles.size()==capacity*sizeof(Grain),"Invalid captured particle ABI");std::vector<Grain> result(capacity);
    if(capture.soa){
        std::vector<std::array<float,4>> fields(4*capacity);std::memcpy(fields.data(),capture.particles.data(),capture.particles.size());
        for(unsigned i=0;i<capacity;++i)result[i]={fields[i],fields[capacity+i],fields[2*capacity+i],fields[3*capacity+i]};
    }else std::memcpy(result.data(),capture.particles.data(),capture.particles.size());
    return result;
}
struct Box {int x0{},y0{},x1{},y1{};};
Box window(const Grain& p,float reach,float displacement){
    reach+=displacement+std::hypot(p.residual[0],p.residual[1])+.001F;
    return {std::clamp(static_cast<int>(std::floor(p.position[0]-reach)),0,width-1),std::clamp(static_cast<int>(std::floor(p.position[1]-reach)),0,height-1),
        std::clamp(static_cast<int>(std::floor(p.position[0]+reach)),0,width-1),std::clamp(static_cast<int>(std::floor(p.position[1]+reach)),0,height-1)};
}
unsigned cell(float x,float y){return static_cast<unsigned>(std::clamp(static_cast<int>(std::floor(y)),0,height-1)*width+std::clamp(static_cast<int>(std::floor(x)),0,width-1));}
unsigned candidates(const Box& box,const std::vector<unsigned>& starts){
    unsigned count=0;for(int row=box.y0;row<=box.y1;++row)count+=starts[static_cast<unsigned>(row*width+box.x1+1)]-starts[static_cast<unsigned>(row*width+box.x0)];return count;
}
bool contains(const Box& box,unsigned c){const auto x=static_cast<int>(c%width),y=static_cast<int>(c/width);return x>=box.x0&&x<=box.x1&&y>=box.y0&&y<=box.y1;}
struct CaptureResult {unsigned active{};float displacement{};std::uint64_t missed{};};
CaptureResult analyze_capture(const SpatialCapture& capture,std::ostream& summary,std::ostream& histogram,unsigned number){
    const auto state=unpack(capture);require(capture.cells.size()>=16+3*cells_count+480&&capture.index.size()==6*capacity,"Invalid captured spatial ABI");
    const float dmax=std::bit_cast<float>(capture.cells[0]);require(std::isfinite(dmax)&&dmax>=0,"Nonfinite captured displacement bound");
    std::vector<unsigned> ids,starts(cells_count+1),fresh(cells_count+1),anchor_cell(capacity),fresh_order(capacity),seen(capacity);
    float residual_max=0;
    for(unsigned id=0;id<capacity;++id)if(state[id].motion[3]>.5F){
        ids.push_back(id);for(float v:state[id].position)require(std::isfinite(v),"Nonfinite captured position");
        for(float v:state[id].residual)require(std::isfinite(v),"Nonfinite captured compensation");
        ++fresh[cell(state[id].position[0],state[id].position[1])+1];residual_max=std::max(residual_max,std::hypot(state[id].residual[0],state[id].residual[1]));
    }
    const auto active=static_cast<unsigned>(ids.size());starts.back()=active;
    for(unsigned c=0;c<cells_count;++c)starts[c]=capture.cells[start_base+c]+capture.cells[offset_base+c/256];
    require(starts.front()==0,"Captured prefix does not begin at zero");
    for(unsigned c=0;c<cells_count;++c){
        require(starts[c]<=starts[c+1]&&starts[c+1]<=active,"Invalid captured cell prefix");
        require(starts[c+1]-starts[c]==capture.cells[16+c],"Captured histogram and prefix disagree");
        for(unsigned k=starts[c];k<starts[c+1];++k){
            const auto id=capture.index[k];require(id<capacity&&state[id].motion[3]>.5F&&seen[id]++==0,"Captured order has stale or duplicate IDs");
            require(capture.index[capacity+id]==k,"Captured reverse map is stale");anchor_cell[id]=c;
            std::array<float,4> anchor{};std::memcpy(anchor.data(),capture.index.data()+2*capacity+4*id,16);
            require(cell(anchor[0],anchor[1])==c,"Captured anchor is in the wrong cell");
            const auto& p=state[id];const double moved=std::hypot((static_cast<double>(p.position[0])-anchor[0])+p.residual[0],(static_cast<double>(p.position[1])-anchor[1])+p.residual[1]);
            require(moved<=dmax+.00001,"Captured Dmax excludes a current endpoint");
        }
    }
    require(std::ranges::count(seen,1U)==active,"Captured index loses active membership");
    std::partial_sum(fresh.begin(),fresh.end(),fresh.begin());auto cursor=fresh;
    for(auto id:ids)fresh_order[cursor[cell(state[id].position[0],state[id].position[1])]++]=id;
    CaptureResult result{active,dmax,0};
    for(float reach:{1.F,2.F}){
        std::vector<double> global_counts,fresh_counts,near_counts;std::map<std::tuple<unsigned,unsigned,unsigned>,unsigned> distribution;
        std::uint64_t global_total=0,fresh_total=0,near_total=0,missed=0;
        for(auto id:ids){
            const auto& p=state[id];const auto box=window(p,reach,dmax),fresh_box=window(p,reach,residual_max);
            const auto global_count=candidates(box,starts),fresh_count=candidates(fresh_box,fresh);unsigned close_count=0;
            const auto check=[&](unsigned other){
                if(id==other)return;
                const auto& q=state[other];const double dx=(static_cast<double>(p.position[0])-q.position[0])+(static_cast<double>(p.residual[0])-q.residual[0]);
                const double dy=(static_cast<double>(p.position[1])-q.position[1])+(static_cast<double>(p.residual[1])-q.residual[1]);
                if(dx*dx+dy*dy<reach*reach){++close_count;if(!contains(box,anchor_cell[other]))++missed;}
            };
            if(active<=256){for(auto other:ids)check(other);} // independent all-pairs for the small adversarial fixtures
            else for(int row=fresh_box.y0;row<=fresh_box.y1;++row)
                for(unsigned k=fresh[static_cast<unsigned>(row*width+fresh_box.x0)];k<fresh[static_cast<unsigned>(row*width+fresh_box.x1+1)];++k)check(fresh_order[k]);
            global_total+=global_count;fresh_total+=fresh_count;near_total+=close_count;
            global_counts.push_back(global_count);fresh_counts.push_back(fresh_count);near_counts.push_back(close_count);++distribution[{global_count,fresh_count,close_count}];
        }
        summary<<number<<','<<capture.label<<','<<capture.generation<<','<<capture.tick<<','<<capture.ordinal<<','<<capture.phase<<','<<capture.iteration<<','<<reach<<','<<active<<','<<dmax<<','<<capture.cells[1]
            <<','<<global_total<<','<<fresh_total<<','<<(fresh_total?static_cast<double>(global_total)/static_cast<double>(fresh_total):0)<<','<<near_total<<','<<missed<<','<<(active<=256?"all_pairs":"fresh_cpu_grid");
        for(const auto* values:{&global_counts,&fresh_counts,&near_counts}){
            if(values->empty())summary<<",0,0,0";else summary<<','<<percentile(*values,.5)<<','<<percentile(*values,.95)<<','<<*std::ranges::max_element(*values);
        }
        summary<<'\n';for(const auto& [key,count]:distribution){const auto [global,fresh_count,close_count]=key;histogram<<number<<','<<reach<<','<<global<<','<<fresh_count<<','<<close_count<<','<<count<<'\n';}
        result.missed+=missed;
    }
    require(result.missed==0,"Captured global window misses geometric neighbors");return result;
}
void write_trace(const TraceReport& trace,const std::filesystem::path& output){
    std::filesystem::create_directories(output);std::ofstream events{output/"dispatches.csv"},summary{output/"snapshots.csv"},histogram{output/"snapshot-histograms.csv"},metadata{output/"trace.txt"};
    events<<"generation,tick,ordinal,phase,iteration,family,operation,sampled,milliseconds,indexed,displacement,builds\n"<<std::setprecision(10);
    const bool bounds=trace.mode==CostMode::bounds||trace.mode==CostMode::snapshots;
    for(const auto& p:trace.dispatches){
        events<<p.generation<<','<<p.tick<<','<<p.ordinal<<','<<p.phase<<','<<p.iteration<<','<<pass_family_names[static_cast<unsigned>(p.family)]<<','<<p.operation<<','<<p.sampled<<',';
        if(p.sampled)events<<p.milliseconds;events<<','<<p.indexed<<',';
        if(bounds&&p.indexed)events<<p.displacement<<','<<p.builds;else events<<',';events<<'\n';
    }
    summary<<"snapshot,label,generation,tick,ordinal,phase,iteration,reach,active,dmax,builds,global_candidates,fresh_candidates,global_to_fresh,geometric_neighbors,missed,reference,global_median,global_p95,global_max,fresh_median,fresh_p95,fresh_max,near_median,near_p95,near_max\n"<<std::setprecision(10);
    histogram<<"snapshot,reach,global_candidates,fresh_candidates,geometric_neighbors,frequency\n";
    unsigned number=0;for(const auto& capture:trace.captures)static_cast<void>(analyze_capture(capture,summary,histogram,number++));
    metadata<<"mode="<<cost_mode_name(trace.mode)<<"\nevents="<<trace.dispatches.size()<<"\ntimestamp_pairs="<<trace.timestamp_pairs<<"\nheader_copies="<<trace.header_copies
        <<"\nsnapshot_copies="<<trace.snapshot_copies<<"\nsnapshots="<<trace.captures.size()<<"\nstaging_bytes="<<trace.staging_bytes
        <<"\nreadback=after explicit glFinish outside the measured batch\ntimestamp_scope=dispatch and following barrier, excluding later trace copies; diagnostic estimates include query effects\n"
        <<"candidate_scope=CPU reconstruction of captured index windows; fresh current-position grid is a diagnostic comparison, not a proposed solver\n";
    require(events.good()&&summary.good()&&histogram.good()&&metadata.good(),"Cannot finish diagnostic trace files");
}
Grain particle(float x,float y,unsigned kind){Grain p;p.position={x,y,x,y};p.motion={0,0,kind==5?2.65F:1.F,static_cast<float>(kind)};return p;}
void clear(ParticleWorld& world){static_cast<void>(world.take_timings());static_cast<void>(world.take_cpu_timings());static_cast<void>(world.take_step_profiles());static_cast<void>(world.take_group_profiles());}
void validate_trace(const std::filesystem::path& shaders,const std::filesystem::path& output,const ExecutionOptions& layout){
    std::ofstream report{output/"cost-tests.txt"};
    const auto create=[&](){ProfileOptions quiet;quiet.collect=true;quiet.gpu_timers=false;quiet.readback_period=0;auto made=ParticleWorld::create(shaders,layout,quiet);require(made.has_value(),made?"":made.error().c_str());return std::move(*made);};
    std::vector<Grain> initial(256);initial[0]=particle(30.2F,80.1F,5);initial[1]=particle(31.1F,80.1F,1);initial[255]=particle(128.2F,80.1F,5);
    initial[0].residual={.0000004F,-.0000002F,0,0};std::vector<std::uint32_t> walls(width*height);
    for(int y=180;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    auto reference=create();reference->load(initial,walls);static_cast<void>(reference->read_neighbors());
    std::vector<std::array<float,4>> delta(256);delta[255]={-96.2F,0,0,0};reference->displace_for_test(delta);
    for(unsigned tick=0;tick<4;++tick)reference->step();const auto expected=reference->readback();
    for(const auto mode:{CostMode::off,CostMode::passes,CostMode::bounds,CostMode::snapshots}){
        auto world=create();world->load(initial,walls);static_cast<void>(world->read_neighbors());
        TraceConfig config;config.mode=mode;config.max_events=1024;config.max_samples=8;config.max_snapshots=2;
        const auto before=world->counters();world->start_diagnostic_trace(config);
        if(mode==CostMode::off)require((world->counters()-before).api_calls==0,"Disabled trace allocated GL resources");
        world->displace_for_test(delta);if(mode==CostMode::snapshots)world->capture_diagnostic_snapshot("large_outlier");
        for(unsigned tick=0;tick<4;++tick)world->step();if(mode==CostMode::snapshots)world->capture_diagnostic_snapshot("after_four_steps");
        drain(*world);const auto actual=world->readback();double max_error=0;
        for(unsigned id=0;id<capacity;++id){
            require(actual[id].motion[3]==expected[id].motion[3],"Diagnostic capture changed membership");if(actual[id].motion[3]<.5F)continue;
            for(const auto pair:{std::pair{actual[id].position,expected[id].position},std::pair{actual[id].motion,expected[id].motion},std::pair{actual[id].residual,expected[id].residual},std::pair{actual[id].contact,expected[id].contact}})
                for(unsigned k=0;k<4;++k){require(std::isfinite(pair.first[k]),"Diagnostic capture produced nonfinite state");max_error=std::max(max_error,std::abs(static_cast<double>(pair.first[k])-pair.second[k]));}
        }
        require(max_error<.0001,"Diagnostic capture changed the authoritative state");const auto trace=world->read_diagnostic_trace();
        require(mode==CostMode::off?trace.dispatches.empty():!trace.dispatches.empty(),"Diagnostic enable policy is incorrect");
        if(mode==CostMode::passes)require(trace.timestamp_pairs==4,"Sparse pass trace lost a sampled tick");
        if(mode==CostMode::bounds||mode==CostMode::snapshots){
            require(trace.header_copies>0,"Missing intermediate displacement copies");
            require(std::ranges::any_of(trace.dispatches,[](const auto& p){return p.indexed&&p.displacement>96;}),"Intermediate trace missed forced distant outlier");
        }
        if(mode==CostMode::snapshots)require(trace.captures.size()==2,"Explicit snapshots were lost");
        write_trace(trace,output/std::string{cost_mode_name(mode)});
        const auto after=world->counters();require(world->read_diagnostic_trace().dispatches.empty()&&(world->counters()-after).api_calls==0,"Consumed trace still performs GL work");
        report<<"mode="<<cost_mode_name(mode)<<" max_state_error="<<max_error<<" passed\n";report.flush();
    }
    auto reset=create();reset->load(initial,walls);static_cast<void>(reset->read_neighbors());
    TraceConfig config;config.mode=CostMode::bounds;config.max_events=1024;reset->start_diagnostic_trace(config);reset->step();reset->reset(Scene::empty);reset->step();drain(*reset);
    const auto generations=reset->read_diagnostic_trace();require(std::ranges::any_of(generations.dispatches,[&](const auto& p){return p.generation!=generations.dispatches.front().generation;}),"Reset lost trace generation tags");
    config.mode=CostMode::passes;config.max_events=1;reset->start_diagnostic_trace(config);bool overflow=false;
    try{reset->step();}catch(const std::runtime_error&){overflow=true;}require(overflow,"Diagnostic capacity silently truncated the trace");glFinish();static_cast<void>(reset->read_diagnostic_trace());
    require(glGetError()==GL_NO_ERROR,"OpenGL error in diagnostic trace validation");report<<"generation_reset=passed\ncapacity_overflow=explicit_failure\nstatus=passed\n";
}
void setup(ParticleWorld& world,unsigned count){
    if(count==3096){world.reset_mission();return;}
    std::vector<Grain> particles(count);std::vector<std::uint32_t> walls(width*height);
    for(int y=187;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    for(unsigned i=0;i<count;++i)particles[i]=particle(2.5F+static_cast<float>(i%300),2.5F+static_cast<float>(i/300),1);
    world.load(particles,walls);MissionConfig config;config.spawn_x=310;world.configure_mission(config);
}
}
std::expected<void,std::string> profile_cost(const std::filesystem::path& output,const ExecutionOptions& layout,CostMode mode,bool check_only){
    try{
        require(layout.neighbors==NeighborMode::reused,"Cost diagnostics require the production reused index");std::filesystem::create_directories(output);
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        if(check_only){validate_trace(shaders,output,layout);return {};}
        std::ofstream environment{output/"environment.txt"},summary{output/"summary.csv"},validation{output/"validation.csv"};
        environment<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))
            <<"\ncost_mode="<<cost_mode_name(mode)<<"\nmode=unpaced\nwarmup_ticks=120\nmeasured_ticks=240\ngpu_timer_mode=elapsed\nreadback_period=2\ncounters=1\ngroup_timers=0\nsoa="<<layout.soa
            <<"\nneighbor_mode=2\nneighbor_reads_requested="<<neighbor_reads_name(layout.neighbor_reads)<<"\nphase_gating="<<layout.phase_gating<<"\nskip_zero_projection="<<layout.skip_zero_projection<<'\n';
        summary<<"scene,metric,samples,median,p95,p99,max\n"<<std::setprecision(10);validation<<"scene,initial_active,final_active,finite,steps,trace_events,timestamp_pairs,header_copies,snapshots,staging_bytes\n";
        for(unsigned scenario=0;scenario<4;++scenario){
            const unsigned count=scenario==1?16000U:scenario==2?32768U:3096U;const std::string scene=scenario==0?"mission":scenario==1?"stress_16k":scenario==2?"stress_32k":"mission_blast";
            ProfileOptions options;options.collect=true;auto made=ParticleWorld::create(shaders,layout,options);require(made.has_value(),made?"":made.error().c_str());auto& world=**made;
            if(scenario==0)environment<<"storage_requested="<<device_storage_name(layout.storage)<<"\nstorage_effective="<<device_storage_name(world.upload_stats().mode)
                <<"\nneighbor_reads_effective="<<neighbor_reads_name(effective_neighbor_reads(layout.neighbor_reads,layout.neighbors))<<"\ncoupling_mode="<<static_cast<int>(layout.coupling)<<'\n';
            setup(world,count);const auto run=[&](unsigned ticks,bool blast){for(unsigned tick=0;tick<ticks;++tick){if(blast&&(tick==0||tick==64))world.queue_explosion({100+tick,128,147,22,70});world.step();if(tick%16==15)drain(world);}drain(world);};
            run(120,false);clear(world);world.start_diagnostic_trace(TraceConfig{.mode=mode});
            const auto started=SDL_GetTicksNS();run(240,scenario==3);const double wall_ms=static_cast<double>(SDL_GetTicksNS()-started)/1e6;
            const auto metric=[&](const char* name,const std::vector<double>& values){require(!values.empty(),"Cost benchmark lost all samples");summary<<scene<<','<<name<<','<<values.size()<<','<<percentile(values,.5)<<','<<percentile(values,.95)<<','<<percentile(values,.99)<<','<<*std::ranges::max_element(values)<<'\n';};
            const auto cpu=world.take_cpu_timings(),gpu=world.take_timings();require(cpu.size()==240&&gpu.size()==240,"Cost benchmark lost measured tick timings");
            metric("gpu_step_ms",gpu);metric("cpu_submit_ms",cpu);metric("batch_mean_step_wall_ms",{wall_ms/240});
            write_profile_samples(world,output/scene);auto trace=world.read_diagnostic_trace();
            unsigned alive=0;bool finite=true;for(const auto& p:world.readback())if(p.motion[3]>.5F){++alive;for(float x:p.position)finite=finite&&std::isfinite(x);for(float x:p.motion)finite=finite&&std::isfinite(x);}
            require(finite&&(scenario==3?alive>=count:alive==count),"Cost benchmark changed membership or produced nonfinite state");
            validation<<scene<<','<<count<<','<<alive<<','<<finite<<','<<world.metrics().steps<<','<<trace.dispatches.size()<<','<<trace.timestamp_pairs<<','<<trace.header_copies<<','<<trace.captures.size()<<','<<trace.staging_bytes<<'\n';
            environment<<"scene="<<scene<<" storage_effective="<<device_storage_name(world.upload_stats().mode)<<" neighbor_reads_effective="<<neighbor_reads_name(effective_neighbor_reads(layout.neighbor_reads,layout.neighbors))<<'\n';
            write_trace(trace,output/scene);summary.flush();validation.flush();environment.flush();require(glGetError()==GL_NO_ERROR,"OpenGL error in cost benchmark");
        }
        require(environment.good()&&summary.good()&&validation.good(),"Cannot finish cost benchmark files");return {};
    }catch(const std::exception& error){std::ofstream{output/"cost-error.txt"}<<error.what();return std::unexpected{error.what()};}
}
}
