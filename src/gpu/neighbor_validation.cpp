#include "gpu/world.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error{message};}
void drain(ParticleWorld& world){glFinish();world.poll();}
double percentile(std::vector<double> values,double q){
    std::ranges::sort(values);return values.empty()?0:values[std::min(values.size()-1,static_cast<std::size_t>(q*static_cast<double>(values.size())))];
}
Grain particle(float x,float y,unsigned type=5){Grain p;p.position={x,y,x,y};p.motion={0,0,type==5?2.65F:1.F,static_cast<float>(type)};return p;}
bool visible_cpu(const Grain& a,const Grain& b,const std::vector<std::uint32_t>& walls){
    // Independent segment/AABB intersection, without the GPU grid walk or cache.
    const double ax=a.position[0],ay=a.position[1],dx=b.position[0]-ax,dy=b.position[1]-ay;
    for(int y=std::max(0,static_cast<int>(std::floor(std::min(ay,ay+dy)))-1);y<=std::min(height-1,static_cast<int>(std::floor(std::max(ay,ay+dy)))+1);++y)
        for(int x=std::max(0,static_cast<int>(std::floor(std::min(ax,ax+dx)))-1);x<=std::min(width-1,static_cast<int>(std::floor(std::max(ax,ax+dx)))+1);++x){
            if(!walls[y*width+x])continue;
            double low=0,high=1;
            const auto axis=[&](double origin,double delta,double edge){
                if(std::abs(delta)<1e-12)return origin>=edge&&origin<=edge+1;
                double lo=(edge-origin)/delta,hi=(edge+1-origin)/delta;if(lo>hi)std::swap(lo,hi);
                low=std::max(low,lo);high=std::min(high,hi);return low<=high;
            };
            if(axis(ax,dx,x)&&axis(ay,dy,y))return false;
        }
    return true;
}
void check_index(ParticleWorld& world){
    const auto state=world.readback();const auto index=world.read_neighbors();
    std::vector<unsigned> seen(capacity);unsigned active=0;
    for(const auto& p:state)if(p.motion[3]>.5F)++active;
    require(index.starts.front()==0&&index.starts.back()==active,"Prefix total differs from active membership");
    for(unsigned cell=0;cell<width*height;++cell){
        require(index.starts[cell+1]-index.starts[cell]==index.counts[cell],"Cell range overlaps or has a gap");
        for(unsigned k=index.starts[cell];k<index.starts[cell+1];++k){
            const auto id=index.order[k];require(id<capacity&&seen[id]++==0&&state[id].motion[3]>.5F,"Sorted view duplicates, loses or resurrects a stable ID");
            require(index.indices[id]==k,"ID to sorted-index mapping is inconsistent");
            const auto& a=index.anchors[id];const auto anchor_cell=std::clamp(static_cast<int>(std::floor(a[1])),0,height-1)*width+std::clamp(static_cast<int>(std::floor(a[0])),0,width-1);
            require(anchor_cell==static_cast<int>(cell),"Scatter placed an ID in the wrong cell");
            const auto& p=state[id];const double moved=std::hypot(static_cast<double>(p.position[0])-a[0]+p.residual[0],static_cast<double>(p.position[1])-a[1]+p.residual[1]);
            require(moved<=index.displacement+.00001,"GPU displacement bound excludes a current endpoint");
        }
    }
    require(std::ranges::count(seen,1U)==active,"Sorted view lost active IDs");
}
void check_pairs(ParticleWorld& world,const std::vector<std::uint32_t>& walls){
    const auto pairs=world.audit_neighbors();const auto state=world.readback();check_index(world);
    for(unsigned i=0;i<256;++i)for(unsigned j=0;j<256;++j){
        const auto& p=state[i];const auto& q=state[j];
        const double dx=(static_cast<double>(p.position[0])-q.position[0])+(static_cast<double>(p.residual[0])-q.residual[0]);
        const double dy=(static_cast<double>(p.position[1])-q.position[1])+(static_cast<double>(p.residual[1])-q.residual[1]);
        const bool close=i!=j&&p.motion[3]>.5F&&q.motion[3]>.5F&&dx*dx+dy*dy<4;
        require((pairs[i*256+j][0]>.5F)==close,"GPU row query differs from independent all-pairs search");
        if(close)require((pairs[i*256+j][1]>.5F)==visible_cpu(p,q,walls),"GPU visibility differs from independent segment/AABB test");
    }
}
void scene(ParticleWorld& world,unsigned count){
    if(count==3096){world.reset_mission();return;}
    std::vector<Grain> particles(count);std::vector<std::uint32_t> walls(width*height);
    for(int y=187;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    for(unsigned i=0;i<count;++i)particles[i]=particle(2.5F+static_cast<float>(i%300),2.5F+static_cast<float>(i/300),1);
    world.load(particles,walls);MissionConfig config;config.spawn_x=310;world.configure_mission(config);
}
void clear(ParticleWorld& world){
    static_cast<void>(world.take_timings());static_cast<void>(world.take_cpu_timings());
    static_cast<void>(world.take_step_profiles());static_cast<void>(world.take_group_profiles());
}
}
std::expected<void,std::string> validate_neighbors(const std::filesystem::path& output,bool benchmark){
    try {
        std::filesystem::create_directories(output);std::ofstream checks{output/"neighbor-tests.txt"};
        const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
        const auto create=[&](const ExecutionOptions& options){auto made=ParticleWorld::create(shaders,options);if(!made)throw std::runtime_error{made.error()};return std::move(*made);};
        for(bool soa:{false,true})for(auto mode:{NeighborMode::sorted,NeighborMode::reused}){
            ExecutionOptions options;options.soa=soa;options.neighbors=mode;auto world=create(options);
            std::vector<std::uint32_t> walls(width*height);std::vector<Grain> particles(256);
            // Sparse stable IDs, outer corners, row boundaries, scan block
            // boundaries, one-cell walls, exact corner occlusion and coincidences.
            for(unsigned i=0;i<240;++i)if(i%7!=0){
                const float x=96.13F+static_cast<float>((i*71)%113)*.061F,y=76.27F+static_cast<float>((i*43)%127)*.059F;
                particles[i]=particle(x,y,i%2?5:1);
            }
            particles[240]=particle(.47F,.47F);particles[241]=particle(1.31F,1.29F);
            particles[242]=particle(319.51F,191.51F);particles[243]=particle(318.57F,190.63F);
            particles[244]=particle(255.61F,1.51F);particles[245]=particle(256.32F,1.48F);
            particles[246]=particle(40.5F,40.5F);particles[247]=particle(41.5F,41.5F);walls[40*width+41]=1;
            particles[248]=particle(50.01F,50.07F);particles[249]=particles[248];
            for(int y=74;y<87;++y)walls[y*width+100]=1;
            world->load(particles,walls);check_pairs(*world,walls);
            std::vector<std::array<float,4>> deltas(256);
            for(unsigned i=0;i<240;++i)if(particles[i].motion[3]>.5F)deltas[i]={i%2?4.23F:-3.17F,i%3?-.37F:.61F,0,0};
            world->displace_for_test(deltas);check_pairs(*world,walls);
            if(mode==NeighborMode::reused){const auto stats=world->read_neighbors();require(stats.builds==1&&stats.displacement>4,"Large correction did not exercise extended-range fallback");}
            world->load(particles,walls);for(unsigned i=0;i<4;++i){world->project_liquids_for_test(3);check_pairs(*world,walls);}
            // Dense cells have no per-cell capacity or candidate cap.
            for(unsigned count:{0U,255U,256U,257U,static_cast<unsigned>(capacity)}){
                std::vector<Grain> dense(count,particle(120.37F,80.23F));world->load(dense,walls);check_index(*world);
                const auto index=world->read_neighbors();require(index.counts[80*width+120]==count,"Dense-cell list was truncated");
                if(count<=256)check_pairs(*world,walls);
            }
            world->load({},walls);world->paint({150,120,155,120,3,Material::water});check_index(*world);
            const auto spawned=world->readback();require(std::ranges::any_of(spawned,[](const Grain& p){return p.motion[3]>.5F;}),"Spawn test created no particles");
            world->paint({150,120,155,120,8,Material::erase});world->step();check_index(*world);
            const auto erased=world->readback();require(std::ranges::none_of(erased,[](const Grain& p){return p.motion[3]>.5F;}),"Deleted stable ID reappeared");
            world->reset_mission();ProfileOptions profile;profile.collect=true;world->set_profile_options(profile);world->step();
            require(world->step_profile().calls.bins==(mode==NeighborMode::reused?2U:43U),"Production schedule rebuilt an unexpected number of indices");
            checks<<"layout="<<(soa?"soa":"aos")<<" mode="<<(mode==NeighborMode::sorted?"sorted":"reused")<<" all_pairs,visibility,large_corrections,projection,dense_capacity,spawn,erase,reset=passed\n";checks.flush();
        }
        require(glGetError()==GL_NO_ERROR,"OpenGL error during neighbor validation");checks<<"status=passed\n";checks.close();
        if(!benchmark)return {};
        struct Variant {const char* name;ExecutionOptions options;};
        ExecutionOptions linked;linked.neighbors=NeighborMode::linked;linked.soa=false;
        auto sorted=linked;sorted.neighbors=NeighborMode::sorted;auto reused=sorted;reused.neighbors=NeighborMode::reused;
        auto soa=reused;soa.soa=true;auto linked_soa=linked;linked_soa.soa=true;
        std::vector<Variant> variants{{"i02_linked_aos",linked},{"sorted_fresh_aos",sorted},{"reused_aos",reused},{"reused_soa",soa},{"linked_soa",linked_soa}};
        std::ofstream summary{output/"summary.csv"},steps{output/"steps.csv"},progress{output/"progress.txt"},environment{output/"environment.txt"};
        environment<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<"\nrepeats=3\nwarmup=120\nmeasured=240\nfresh_world_per_variant=1\nreadback=auto,period=2\ngroup_timers=1\nmode=unpaced; glFinish and HUD poll every 16 ticks outside CPU step timer\n";
        summary<<"scene,variant,repeat,metric,samples,median,p95,p99,max\n";
        steps<<"scene,variant,repeat,tick,total_ms,poll_ms,solver_ms";
#define HEADER(name) steps<<"," #name;
        STREFA_GPU_COUNTERS(HEADER)
#undef HEADER
        steps<<'\n';
        const auto metric=[&](unsigned count,const char* variant,unsigned repeat,const char* name,const std::vector<double>& values){
            if(values.empty())return;
            summary<<count<<','<<variant<<','<<repeat<<','<<name<<','<<values.size()<<','<<percentile(values,.5)<<','<<percentile(values,.95)<<','<<percentile(values,.99)<<','<<*std::ranges::max_element(values)<<'\n';
        };
        for(unsigned repeat=1;repeat<=3;++repeat){
            if(repeat%2==0)std::ranges::reverse(variants);
            for(const auto& variant:variants)for(unsigned count:{3096U,16000U,32768U}){
                progress<<"repeat="<<repeat<<" variant="<<variant.name<<" count="<<count<<'\n';progress.flush();
                auto world=create(variant.options);ProfileOptions profile;profile.collect=true;profile.group_timers=true;world->set_profile_options(profile);scene(*world,count);
                std::vector<double> external;
                const auto run=[&](unsigned ticks){for(unsigned i=0;i<ticks;++i){world->step();if(i%16==15){glFinish();const auto began=SDL_GetTicksNS();world->poll();external.push_back(static_cast<double>(SDL_GetTicksNS()-began)/1e6);}}drain(*world);};
                run(120);clear(*world);external.clear();const auto began=SDL_GetTicksNS();run(240);
                metric(count,variant.name,repeat,"batch_mean_step_wall_ms",{static_cast<double>(SDL_GetTicksNS()-began)/1e6/240});
                metric(count,variant.name,repeat,"external_poll_ms",external);metric(count,variant.name,repeat,"gpu_step_ms",world->take_timings());metric(count,variant.name,repeat,"cpu_submit_ms",world->take_cpu_timings());
                const auto samples=world->take_step_profiles();require(samples.size()==240,"Benchmark lost measured CPU samples");
                const auto groups=world->take_group_profiles();require(groups.size()==240,"Benchmark lost measured GPU group samples");
                for(std::size_t i=0;i<gpu_group_names.size();++i){std::vector<double> values;for(const auto& group:groups)values.push_back(group.milliseconds[i]);metric(count,variant.name,repeat,((std::string{"gpu_"}+std::string{gpu_group_names[i]})+"_ms").c_str(),values);}
                for(const auto& p:samples){steps<<count<<','<<variant.name<<','<<repeat<<','<<p.tick<<','<<p.total_ms<<','<<p.poll_ms<<','<<p.solver_ms;
#define VALUE(name) steps<<','<<p.calls.name;
                    STREFA_GPU_COUNTERS(VALUE)
#undef VALUE
                    steps<<'\n';}
                if(variant.options.neighbors!=NeighborMode::linked){
                    const auto index=world->read_neighbors();metric(count,variant.name,repeat,"gpu_actual_index_builds",{static_cast<double>(index.builds)});metric(count,variant.name,repeat,"final_displacement_bound",{index.displacement});
                    const auto counts=world->audit_neighbors(false);std::vector<double> candidates,neighbors;
                    const auto state=world->readback();for(unsigned i=0;i<capacity;++i)if(state[i].motion[3]>.5F){candidates.push_back(counts[i][0]);neighbors.push_back(counts[i][1]);}
                    metric(count,variant.name,repeat,"candidate_count",candidates);metric(count,variant.name,repeat,"neighbor_count",neighbors);
                }
                summary.flush();steps.flush();
            }
            if(repeat%2==0)std::ranges::reverse(variants);
        }
        return {};
    }catch(const std::exception& e){std::ofstream{output/"neighbor-error.txt"}<<e.what();return std::unexpected{e.what()};}
}
}
