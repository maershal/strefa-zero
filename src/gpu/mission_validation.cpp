#include "gpu/world.hpp"
#include "sim/fixed_clock.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace strefa::gpu {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error{message};}
void finite(const MissionSnapshot& g){for(float v:g.actor)require(std::isfinite(v),"Nonfinite player");}
void ticks(ParticleWorld& w,int n,MissionInput input={}){
    for(int i=0;i<n;++i){w.step(input);if(i%16==15){glFinish();w.poll();}}
    glFinish();w.poll();finite(w.read_mission());
}
std::vector<std::uint32_t> floor_at(int y){std::vector<std::uint32_t> walls(width*height);for(int j=y;j<height;++j)for(int x=0;x<width;++x)walls[j*width+x]=1;return walls;}
MissionInput route(unsigned tick){
    if(tick<60)return {0,54,177,fire};
    if(tick<400)return {1,164,177,(tick<240?jet:0U)|(tick==60?jump:0U)};
    if(tick<460)return {0,164,177,fire};
    if(tick<580)return {0,132,145,fire};
    if(tick<790)return {1,276,177,0};
    if(tick<850)return {0,276,177,fire};
    if(tick<880)return {0,230,125,reload};
    if(tick<1100)return {0,230,125,fire};
    if(tick<1340)return {1,308,177,(tick<1260?jet:0U)|(tick==1100?jump:0U)};
    return {0,308,177,0};
}
double percentile(std::vector<double> v,double q){if(v.empty())return 0;std::ranges::sort(v);return v[std::min(v.size()-1,static_cast<std::size_t>(q*v.size()))];}
void benchmark(std::ofstream& out,const char* scene,unsigned count,ParticleWorld& world){
    glFinish();world.poll();static_cast<void>(world.take_timings());ticks(world,120);static_cast<void>(world.take_timings());
    static_cast<void>(world.take_cpu_timings());ticks(world,240);auto times=world.take_timings();require(!times.empty(),"No GPU timing samples");
    out<<scene<<','<<count<<','<<percentile(times,.5)<<','<<percentile(times,.95)<<','<<percentile(times,.99)<<','<<*std::ranges::max_element(times)<<std::endl;
    auto cpu=world.take_cpu_timings();out<<scene<<"_cpu_submit,"<<count<<','<<percentile(cpu,.5)<<','<<percentile(cpu,.95)<<','<<percentile(cpu,.99)<<','<<*std::ranges::max_element(cpu)<<std::endl;
    static_cast<void>(world.take_render_timings());for(int i=0;i<120;++i){world.render(1);if(i%16==15){glFinish();world.poll();}}
    glFinish();world.poll();auto render=world.take_render_timings();require(!render.empty(),"No rendering timing samples");
    out<<scene<<"_gpu_render,"<<count<<','<<percentile(render,.5)<<','<<percentile(render,.95)<<','<<percentile(render,.99)<<','<<*std::ranges::max_element(render)<<std::endl;
}
}
MissionInput scripted_mission_input(unsigned tick){return route(tick);}
std::expected<void,std::string> validate_mission(ParticleWorld& world,const std::filesystem::path& output){
    try{
        std::filesystem::create_directories(output);std::ofstream report{output/"gpu-mission-tests.txt"};
        std::ofstream timing{output/"gpu-mission-benchmark.csv"};timing<<"scene,particles,median_ms,p95_ms,p99_ms,max_ms\n";
        // Exercise sparse high IDs and repeated editing, not just a dense prefix.
        world.reset(Scene::empty);
        for(int y=15;y<90;y+=22)world.paint({60.F,static_cast<float>(y),130.F,static_cast<float>(y),9,Material::sand});
        auto painted=world.readback();const auto painted_walls=world.read_terrain();
        const auto painted_count=static_cast<unsigned>(std::ranges::count_if(painted,[](const Grain& p){return p.motion[3]>0;}));
        benchmark(timing,"painted",painted_count,world);
        world.load(painted,std::span{painted_walls}.first(width*height));benchmark(timing,"loaded",painted_count,world);
        for(int repeat=0;repeat<4;++repeat){world.paint({50,30,150,90,16,Material::erase});world.paint({60,35,120,70,12,Material::sand});ticks(world,4);}
        auto edited=world.readback();ticks(world,60);auto after=world.readback();
        for(std::size_t i=0;i<edited.size();++i)require(edited[i].motion[3]==after[i].motion[3],"Inactive particle resurrected");
        report<<"active_edit_conservation=passed\n";
        world.reset(Scene::empty);world.configure_mission({});ticks(world,120);
        auto g=world.read_mission();report<<"floor_y="<<g.actor[1]<<std::endl;
        require(std::abs(g.actor[1]-187)<.01F,"Player cannot stand on terrain");
        ticks(world,45,{0,54,177,fire});g=world.read_mission();report<<"target_score="<<g.inventory[2]<<std::endl;
        require(g.inventory[2]==1,"Rifle did not destroy first target");
        world.step({0,70,150,grenade});ticks(world,260);g=world.read_mission();
        report<<"explosions="<<g.counts[0]<<std::endl;require(g.counts[0]==1,"Grenade event lost or duplicated");
        // A thin wall blocks both a muzzle touching it and fast projectile segments.
        for(int close_muzzle:{0,1}){
            auto shield=floor_at(187);for(int y=130;y<187;++y)shield[y*width+40]=1;
            world.load({},shield);MissionConfig c;c.spawn_x=close_muzzle?35.F:24.F;c.targets[0]={54,177,3,0};world.configure_mission(c);
            ticks(world,60,{0,54,177,fire});require(world.read_mission().inventory[2]==0,"Shot tunneled through a thin wall");
            ticks(world,120,{1,54,177,0});require(world.read_mission().actor[0]<=36.01F,"Player tunneled through wall");
        }
        // Upward sweeps must collide with a one-cell ceiling.
        auto ceiling=floor_at(187);for(int x=1;x<80;++x)ceiling[150*width+x]=1;
        world.load({},ceiling);world.configure_mission({});ticks(world,1);world.step({0,54,177,jump|jet});
        float min_feet=187;for(int i=0;i<120;++i){world.step({0,54,177,jet});min_feet=std::min(min_feet,world.read_mission().actor[1]);}
        report<<"ceiling_min_feet="<<min_feet<<std::endl;require(min_feet>=168.99F,"Player tunneled through ceiling");
        float unshielded=0;
        for(bool shielded:{false,true}){
            auto shield=floor_at(187);if(shielded)for(int y=100;y<187;++y)shield[y*width+62]=1;
            Grain p;p.position={68.5F,160,68.5F,160};p.motion={0,0,1,1};world.load(std::span{&p,1},shield);
            MissionConfig c;c.spawn_x=100;world.configure_mission(c);world.queue_explosion({1,54,160,24,70});world.step();
            const auto result=world.readback();const float vx=result[0].motion[0];
            report<<(shielded?"shielded":"unshielded")<<"_blast_vx="<<vx<<std::endl;
            if(!shielded){unshielded=vx;require(vx>10,"Blast did not push exposed grain");}
            else require(std::abs(vx)<.01F&&vx<unshielded*.01F,"Thin wall did not shield blast");
            world.queue_explosion({1,54,160,24,70});world.step();require(world.read_mission().counts[0]==1,"Duplicate explosion ID applied twice");
        }
        // With no free carriers, damaged terrain must retain its material.
        auto full_walls=floor_at(187);full_walls[160*width+50]=2;
        std::vector<Grain> full(capacity);
        for(int i=0;i<capacity;++i){float x=1.5F+static_cast<float>(i%300),y=1.5F+static_cast<float>(i/300);
            full[i].position={x,y,x,y};full[i].motion={0,0,1,1};}
        world.load(full,full_walls);world.configure_mission({});world.queue_explosion({1,48,160,10,0});world.step();
        g=world.read_mission();require(g.counts[1]==1&&g.counts[2]==1&&g.counts[3]==0,"Full pool discarded destroyed material");
        require(world.read_terrain()[160*width+50]==2,"Pending debris erased its terrain cell");
        world.paint({20,20,25,20,4,Material::erase});world.step();g=world.read_mission();
        require(g.counts[2]==0&&g.counts[3]==1&&world.read_terrain()[160*width+50]==0,"Pending debris did not resume after freeing capacity");
        report<<"debris_capacity_accounting=passed\n";
        for(int kind:{5,6,1}){
            auto walls=floor_at(180);for(int y=110;y<180;++y){walls[y*width+100]=1;walls[y*width+160]=1;}
            std::vector<Grain> particles;
            for(int y=kind==1?164:140;y<180;++y)for(int x=101;x<160;++x){Grain p;p.position={x+.5F,y+.5F,x+.5F,y+.5F};
                p.motion={0,0,kind==5?2.65F:kind==6?3.3125F:1.F,static_cast<float>(kind)};particles.push_back(p);}
            world.load(particles,walls);MissionConfig config;config.spawn_x=130;config.spawn_y=125;world.configure_mission(config);
            ticks(world,900);g=world.read_mission();report<<"material_"<<kind<<"_feet="<<g.actor[1]<<"\nmaterial_"<<kind<<"_speed="<<g.actor[3]<<std::endl;
            world.render(1);world.save_image(output/("body-"+std::to_string(kind)+".bmp"));
            if(kind==5)require(g.actor[1]<174&&g.actor[1]>140,"Water buoyancy failed");
            if(kind==6)require(g.actor[1]>174,"Player should sink in lighter oil");
            if(kind==1){
                require(g.actor[1]<177&&g.locomotion[2]>.5F,"Player sank through sand");
                const auto before=world.readback();const float x=g.actor[0];ticks(world,80,{1,180,150,0});
                const auto moved=world.readback();double shifted=0;
                for(std::size_t i=0;i<before.size();++i)if(before[i].motion[3]==1)shifted+=std::abs(moved[i].position[0]-before[i].position[0]);
                report<<"sand_walk_dx="<<world.read_mission().actor[0]-x<<"\nsand_displacement="<<shifted<<std::endl;
                require(world.read_mission().actor[0]>x+3&&shifted>3,"Walking did not push the sand pile");
            }
        }
        // Tick-indexed script: frame rate controls batching only, never physics input.
        MissionSnapshot reference{};
        for(int fps:{30,60,144}){
            world.reset_mission();sim::FixedStepClock<120,4> clock;unsigned tick=0;std::ofstream history{output/("route-"+std::to_string(fps)+".csv")};
            history<<"tick,x,y,score,broken,debris\n";
            while(tick<1700){const int count=clock.advance(1.0/fps,false,1);
                for(int j=0;j<count&&tick<1700;++j){world.step(route(tick++));if(tick%60==0){auto state=world.read_mission();finite(state);
                    history<<tick<<','<<state.actor[0]<<','<<state.actor[1]<<','<<state.inventory[2]<<','<<state.counts[1]<<','<<state.counts[3]<<std::endl;}}
                if(tick%16<4){glFinish();world.poll();}
            }
            g=world.read_mission();world.render(1);world.save_image(output/("mission-"+std::to_string(fps)+".bmp"));
            report<<"route_"<<fps<<"_score="<<g.inventory[2]<<"\nroute_"<<fps<<"_complete="<<g.status[3]<<"\nroute_"<<fps<<"_broken="<<g.counts[1]<<std::endl;
            require(g.inventory[2]==3&&g.status[3]==1,"Mission route did not complete");
            require(g.counts[1]>=4&&g.counts[3]==g.counts[1],"Route did not exercise destruction/debris");
            const auto terrain=world.read_terrain();unsigned outlet=0,support=0;
            for(int y=111;y<=157;++y)for(int x=132;x<=133;++x)outlet+=terrain[y*width+x]==0;
            for(int y=125;y<=126;++y)for(int x=212;x<=247;++x)support+=terrain[y*width+x]==0;
            auto state=world.readback();unsigned water_out=0,sand_out=0;
            for(const auto& p:state){water_out+=p.motion[3]==5&&p.position[0]>135;sand_out+=p.motion[3]==1&&p.position[1]>130;}
            report<<"outlet_cells="<<outlet<<"\nsupport_cells="<<support<<"\nwater_released="<<water_out<<"\nsand_released="<<sand_out<<std::endl;
            require(outlet>=2&&support>=2&&water_out>20&&sand_out>20,"Dam/silo did not release materials");
            require(clock.dropped_seconds()==0,"Fixed clock dropped script time");
            if(fps==30)reference=g;else{require(g.inventory==reference.inventory,"Frame-dependent weapon events");
                require(std::hypot(g.actor[0]-reference.actor[0],g.actor[1]-reference.actor[1])<2,"Frame-dependent player trajectory");}
        }
        world.reset_mission();benchmark(timing,"mission",world.metrics().grains,world);
        // Stress limits are measured, not advertised as a 60 FPS guarantee.
        for(unsigned count:{16000U,32768U}){
            full.resize(count);for(unsigned i=0;i<count;++i){const float x=1.5F+static_cast<float>(i%300),y=1.5F+static_cast<float>(i/300);
                full[i].position={x,y,x,y};full[i].motion={0,0,1,1};}
            world.load(full,floor_at(187));world.configure_mission({});require(world.metrics().grains==count,"Stress particle count mismatch");
            benchmark(timing,count==16000?"stress_16k":"stress_32k",count,world);
        }
        world.reset_mission();const unsigned epoch=world.mission().status[1];ticks(world,2);world.reset_mission();world.poll();
        require(world.mission().status[1]>epoch&&world.mission().status[2]==0,"Reset accepted stale HUD snapshot");
        report<<"status=passed\n";return {};
    }catch(const std::exception& e){std::ofstream{output/"mission-error.txt"}<<e.what();return std::unexpected{e.what()};}
}
}
