#include "gpu/world.hpp"
#include "sim/fixed_clock.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error{message};}
Grain particle(float x,float y,int kind=5,float vx=0,float vy=0){
    Grain p;p.position={x,y,x,y};p.motion={vx,vy,kind==6?3.3125F:2.65F,static_cast<float>(kind)};return p;
}
void conserved(const std::vector<Grain>& before,const std::vector<Grain>& after){
    for(std::size_t i=0;i<before.size();++i){
        require(before[i].motion[3]==after[i].motion[3]&&before[i].motion[2]==after[i].motion[2],"Liquid particle identity/mass changed");
        if(after[i].motion[3]<.5F)continue;
        for(auto value:after[i].position)require(std::isfinite(value),"Nonfinite liquid position");
        for(auto value:after[i].motion)require(std::isfinite(value),"Nonfinite liquid velocity");
        require(after[i].position[0]>=.319F&&after[i].position[0]<=width-.319F&&after[i].position[1]>=.319F&&after[i].position[1]<=height-.319F,"Liquid escaped domain");
    }
}
double compression(ParticleWorld& world){double result=0;for(const auto& term:world.read_fluid_terms())result=std::max(result,static_cast<double>(term[1])-1);return result;}
double mean_height(const std::vector<Grain>& particles,int material){double sum=0;unsigned n=0;for(const auto& p:particles)if(p.motion[3]==material){sum+=p.position[1];++n;}return sum/std::max(1U,n);}
}
std::expected<void,std::string> validate_liquids(ParticleWorld& world,const std::filesystem::path& output){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"gpu-liquid-tests.txt"};
        report<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nmodel=PBF 2D; h=2; dt=1/240; outer=120Hz\n";
        std::vector<std::uint32_t> walls(width*height);std::vector<Grain> initial;
        // Independent double-precision density sum over all particles (no bins,
        // GLSL, boundaries, or shared C++ kernel implementation).
        for(int y=0;y<15;++y)for(int x=0;x<15;++x)initial.push_back(particle(100+x*.85F,60+y*.85F));
        world.load(initial,walls);const auto terms=world.read_fluid_terms();double error=0;
        for(std::size_t i=0;i<initial.size();++i){
            double rho=0;for(const auto& q:initial){double dx=initial[i].position[0]-q.position[0],dy=initial[i].position[1]-q.position[1];const double r2=dx*dx+dy*dy;if(r2<4)rho+=std::pow(4-r2,3)/(64*std::numbers::pi);}
            error=std::max(error,std::abs(terms[i][1]-rho/1.014612762210833));
        }
        require(error<.00001,"GPU density differs from independent CPU sum");report<<"density_cpu_max_error="<<error<<'\n';
        const double before=compression(world);world.project_liquids_for_test(80);const double after=compression(world);
        report<<"projection_compression_before="<<before<<"\nprojection_compression_after="<<after<<std::endl;
        require(before>.2&&after<.025,"Density projection did not converge");conserved(initial,world.readback());
        // A one-cell wall blocks influence from an otherwise close neighbour.
        for(int y=0;y<height;++y)walls[y*width+156]=1;
        initial={particle(155.65F,60),particle(157.35F,60)};world.load(initial,walls);auto blocked=world.read_fluid_terms();
        initial.resize(1);world.load(initial,walls);auto isolated=world.read_fluid_terms();
        require(std::abs(blocked[0][1]-isolated[0][1])<.000001F,"Density interaction crossed a thin wall");
        // An edited wall must invalidate the visibility cache immediately.
        initial={particle(155.65F,60.8F),particle(157.35F,60.8F)};world.load(initial,walls);
        world.paint({156.5F,60,156.5F,60,1,Material::erase});
        const auto edited_terms=world.read_fluid_terms();conserved(initial,world.readback());
        walls[59*width+156]=0;walls[60*width+156]=0;world.load(initial,walls);
        const auto loaded_terms=world.read_fluid_terms();
        require(std::abs(edited_terms[0][1]-loaded_terms[0][1])<.000001F,"Wall edit left a stale visibility cache");
        walls[59*width+156]=1;walls[60*width+156]=1;
        initial.clear();for(int y=0;y<12;++y)for(int x=0;x<8;++x)initial.push_back(particle(145.F+x,40.F+y,5,80));
        world.load(initial,walls);for(int tick=0;tick<120;++tick)world.step();const auto shielded=world.readback();conserved(initial,shielded);
        for(const auto& p:shielded)if(p.motion[3]>.5F)require(p.position[0]<=155.681F,"Liquid tunneled through thin wall");report<<"thin_wall_transport_and_pressure=passed\n";
        // Distinct materials keep exact carrier counts through edit/reuse.
        world.reset(Scene::empty);world.paint({100,40,100,40,6,Material::water});world.paint({130,40,130,40,6,Material::oil});
        glFinish();world.poll();glFinish();world.poll();require(world.metrics().water>50&&world.metrics().oil>50,"Liquid brush/count failed");
        world.paint({100,40,130,40,9,Material::erase});glFinish();world.poll();glFinish();world.poll();require(world.metrics().grains==0,"Liquid erase failed");
        report<<"liquid_brush_counts=passed\n";
        // Same fixed tick schedule at different presentation rates. A free
        // droplet isolates the integration from nondeterministic neighbour sums.
        std::ranges::fill(walls,0U);float reference{};
        for(const int fps:{30,60,144}){
            initial={particle(80,20)};world.load(initial,walls);sim::FixedStepClock<120,4> clock;
            for(int f=0;f<fps;++f){int steps=clock.advance(1.0/fps,false,1);for(int i=0;i<steps;++i)world.step();}
            require(world.metrics().steps==120&&clock.dropped_seconds()<1e-8,"Liquid tick count depends on FPS");
            float y=world.readback()[0].position[1];if(fps==30)reference=y;else require(std::abs(reference-y)<.001F,"Liquid trajectory depends on FPS");
        }
        report<<"fps_30_60_144=passed\n";
        glFinish();world.poll();static_cast<void>(world.take_timings());
        std::ofstream csv{output/"gpu-liquid-benchmark.csv"};csv<<"scene,particles,ticks,gpu_median_ms,gpu_p95_ms,gpu_p99_ms,gpu_max_ms,max_compression\n";
        for(const auto scene:{Scene::water_tank,Scene::dam_break,Scene::connected_vessels,Scene::oil_water,Scene::sand_water}){
            world.reset(scene);initial=world.readback();const auto count=world.metrics().grains;
            const int ticks=scene==Scene::connected_vessels?1200:480;
            for(int tick=0;tick<ticks;++tick){world.step();if(tick%16==15){glFinish();world.poll();}
                if(tick==119||tick==ticks-1){world.render(1);world.save_image(output/("liquid-"+std::to_string(static_cast<int>(scene))+"-"+std::to_string(tick+1)+".bmp"));}}
            const auto result=world.readback();conserved(initial,result);const double residual=compression(world);
            glFinish();world.poll();auto timings=world.take_timings();require(timings.size()==static_cast<unsigned>(ticks),"Missing liquid timing samples");std::ranges::sort(timings);
            auto percentile=[&](double p){return timings[static_cast<std::size_t>(p*(timings.size()-1))];};
            csv<<static_cast<int>(scene)<<','<<count<<','<<ticks<<','<<percentile(.5)<<','<<percentile(.95)<<','<<percentile(.99)<<','<<timings.back()<<','<<residual<<std::endl;
            report<<"scene_"<<static_cast<int>(scene)<<"_mass=preserved; compression="<<residual<<std::endl;
            require(residual<.05,"Fluid scene exceeds 5 percent density error");
            if(scene==Scene::dam_break){unsigned right=0;for(const auto& p:result)if(p.motion[3]==5&&p.position[0]>150)++right;require(right>500,"Dam break did not flow horizontally");report<<"dam_right_particles="<<right<<'\n';}
            if(scene==Scene::oil_water){report<<"water_mean_y="<<mean_height(result,5)<<"\noil_mean_y="<<mean_height(result,6)<<'\n';require(mean_height(result,5)>mean_height(result,6)+5,"Layered oil/water inverted");}
            if(scene==Scene::sand_water){
                report<<"sand_mean_y="<<mean_height(result,1)<<"\nwater_mixed_mean_y="<<mean_height(result,5)<<std::endl;
                require(mean_height(result,1)>mean_height(result,5)+2,"Sand failed to sink into water");
            }
            if(scene==Scene::connected_vessels){
                double left=0,right=0;unsigned nl=0,nr=0;
                for(const auto& p:result)if(p.motion[3]==5){if(p.position[0]<155){left+=p.position[1];++nl;}if(p.position[0]>159){right+=p.position[1];++nr;}}
                const double difference=2*std::abs(left/nl-right/nr);report<<"vessels_estimated_level_difference_cells="<<difference<<std::endl;
                require(difference<3,"Connected vessels failed to approach equal levels");
            }
        }
        initial.clear();for(int i=0;i<capacity;++i)initial.push_back(particle(5.F+i%300,5.F+i/300));
        std::ranges::fill(walls,0U);world.load(initial,walls);
        for(int tick=0;tick<60;++tick){world.step();if(tick%16==15){glFinish();world.poll();}}
        conserved(initial,world.readback());report<<"liquid_capacity_32768=passed\n";
        require(glGetError()==GL_NO_ERROR,"OpenGL liquid validation error");report<<"status=passed\n";return {};
    }catch(const std::exception& e){return std::unexpected{e.what()};}
}
}
