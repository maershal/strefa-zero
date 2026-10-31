namespace {
void validate_vfx_replay(const std::filesystem::path& output,const ExecutionOptions& execution){
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error{message};};
    const auto shaders=std::filesystem::path{reinterpret_cast<const char8_t*>(SDL_GetBasePath())}/"assets/shaders";
    const auto create=[&](VfxQuality q){ProfileOptions quiet;quiet.gpu_timers=false;quiet.readback_period=0;auto made=ParticleWorld::create(shaders,execution,quiet);require(bool(made),"Cannot create VFX replay world");require((*made)->vfx_options().quality==VfxQuality::off,"Legacy standalone world unexpectedly enabled VFX");(*made)->set_vfx_options({q,false});return std::move(*made);};
    auto source=create(VfxQuality::off);std::array replicas{create(VfxQuality::off),create(VfxQuality::low),create(VfxQuality::high)};
    std::vector<unsigned> walls(width*height);for(int y=180;y<height;++y)for(int x=0;x<width;++x)walls[y*width+x]=1;
    std::vector<Grain> input;for(int y=140;y<179;++y)for(int x=112;x<149;++x){Grain p;p.position={x+.53F,y+.47F,x+.53F,y+.47F};p.motion={0,0,y<160?2.65F:1.F,y<160?5.F:1.F};input.push_back(p);}
    source->load(input,walls);MissionConfig config;config.spawn_x=130;config.spawn_y=145;source->configure_mission(config);
    struct Ledger{double sum{},dx{},dy{},ax{},ay{},mean_max{},center_max{},actor_max{};};std::array<Ledger,3> ledgers;
    std::ofstream csv{output/"vfx-replay.csv"};csv<<std::setprecision(12)<<"tick,quality,mean,center,actor,mean_sum,center_bias,actor_bias\n";
    for(unsigned tick=0;tick<90;++tick){if(tick==45){config.spawn_x=139;config.spawn_y=151;source->configure_mission(config);}const auto before=source->readback();const auto checkpoint=source->read_mission();const auto engine_tick=source->metrics().steps;const MissionInput controls{tick<45?.4F:-.3F,170,140,0};
        require(source->step(controls),"VFX source replay backpressure");const auto expected=source->readback();const auto actor=source->read_mission();
        for(unsigned quality=0;quality<3;++quality){auto& world=*replicas[quality];world.load(before,walls);world.restore_coupling_checkpoint(checkpoint,engine_tick);require(world.step(controls),"VFX replica backpressure");const auto actual=world.readback();const auto observed=world.read_mission();double sum=0,dx=0,dy=0;unsigned alive=0;
            for(unsigned id=0;id<capacity;++id){require(actual[id].motion[3]==expected[id].motion[3]&&actual[id].motion[2]==expected[id].motion[2],"VFX changed per-ID membership or mass");if(actual[id].motion[3]<.5F)continue;++alive;
                const double x=(static_cast<double>(actual[id].position[0])-expected[id].position[0])+(static_cast<double>(actual[id].residual[0])-expected[id].residual[0]);const double y=(static_cast<double>(actual[id].position[1])-expected[id].position[1])+(static_cast<double>(actual[id].residual[1])-expected[id].residual[1]);require(std::isfinite(x)&&std::isfinite(y),"Nonfinite VFX replay");sum+=std::hypot(x,y);dx+=x;dy+=y;}
            const double n=std::max(1u,alive),mean=sum/n,cx=dx/n,cy=dy/n,ax=static_cast<double>(observed.actor[0])-actor.actor[0],ay=static_cast<double>(observed.actor[1])-actor.actor[1];auto& total=ledgers[quality];total.sum+=mean;total.dx+=cx;total.dy+=cy;total.ax+=ax;total.ay+=ay;total.mean_max=std::max(total.mean_max,mean);total.center_max=std::max(total.center_max,std::hypot(cx,cy));total.actor_max=std::max(total.actor_max,std::hypot(ax,ay));
            csv<<tick<<','<<vfx_quality_name(static_cast<VfxQuality>(quality))<<','<<mean<<','<<std::hypot(cx,cy)<<','<<std::hypot(ax,ay)<<','<<total.sum<<','<<std::hypot(total.dx,total.dy)<<','<<std::hypot(total.ax,total.ay)<<'\n';
        }
    }
    std::ofstream result{output/"vfx-replay-summary.csv"};result<<"quality,mean_max,center_max,actor_max,mean_sum,center_bias,actor_bias,accepted\n";
    // These are the existing I04.5-Q strict checkpoint and accumulated-defect limits.
    for(unsigned i=0;i<3;++i){const auto& t=ledgers[i];const bool ok=t.mean_max<.05&&t.center_max<.02&&t.actor_max<.15&&t.sum<.05&&std::hypot(t.dx,t.dy)<.02&&std::hypot(t.ax,t.ay)<.15;result<<vfx_quality_name(static_cast<VfxQuality>(i))<<','<<t.mean_max<<','<<t.center_max<<','<<t.actor_max<<','<<t.sum<<','<<std::hypot(t.dx,t.dy)<<','<<std::hypot(t.ax,t.ay)<<','<<ok<<'\n';require(ok,"VFX replay exceeds unchanged I04.5-Q tolerances");}
}
}
