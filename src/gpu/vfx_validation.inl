std::expected<void,std::string> ParticleWorld::validate_vfx(const std::filesystem::path& output,bool benchmark){
    try{
        std::filesystem::create_directories(output);auto& s=*impl_;auto& pool=s.resources->vfx;auto& gl=s.gl;
        std::ofstream report{output/"vfx-tests.txt"};auto require=[&](bool ok,const char* message){report<<message<<'='<<(ok?"PASS":"FAIL")<<std::endl;if(!ok)throw std::runtime_error{message};};
        std::ofstream environment{output/"vfx-environment.txt"};environment<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<"\ncapacity="<<vfx_capacity<<"\nparticle_bytes="<<sizeof(VfxParticle)<<"\nmemory_bytes="<<pool.memory_bytes()<<"\nphysics=legacy\nrender=basic\n";
        const auto bytes=[&]{std::vector<std::byte> result;gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);for(auto buffer:s.buffers){gl.BindBuffer(GL_COPY_READ_BUFFER,buffer);GLint size{};gl.GetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);const auto at=result.size();result.resize(at+static_cast<std::size_t>(size));gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,size,result.data()+at);}s.invalidate_bindings();return result;};
        const auto check_pool=[&](const VfxSnapshot& snap){std::vector<unsigned char> seen(vfx_capacity);bool ok=snap.alive.size()+snap.free.size()==vfx_capacity&&snap.counters.draw[1]==snap.alive.size()&&snap.counters.layers[0]+snap.counters.layers[1]+snap.counters.layers[2]<=snap.alive.size()&&snap.counters.pre[1]==snap.counters.layers[0]&&snap.counters.post[1]==snap.counters.layers[1]&&snap.counters.wave[1]==snap.counters.layers[2];for(unsigned id:snap.alive){if(id>=vfx_capacity||seen[id]++)ok=false;else for(float value:snap.particles[id].position_velocity)if(!std::isfinite(value))ok=false;}for(unsigned id:snap.free)if(id>=vfx_capacity||seen[id]++)ok=false;return ok&&std::all_of(seen.begin(),seen.end(),[](auto n){return n==1;});};
        std::vector<float> field(320*192*4);std::vector<std::uint16_t> walls(320*192);unsigned tick=0,gen=100;
        const auto advance=[&](std::span<const VfxEvent> events={},float dt=1.F/120.F,bool water=false){pool.begin_tick(0,gen,++tick,dt);pool.upload_field(field,walls);pool.end_tick(0,events,water);s.invalidate_bindings();};
        const auto particle=[](VfxKind kind,float x,float y,float vx=0,float vy=0,float life=1000){VfxParticle p;p.position_velocity={x,y,vx,vy};p.previous_age_life={x,y,0,life};p.appearance={static_cast<unsigned>(kind),0xffffffffu,std::bit_cast<unsigned>(.4F),1};return p;};
        const auto event=[&](VfxKind kind,unsigned id=1){return VfxEvent{{gen,tick+1,id,static_cast<unsigned>(kind)},{60,60,12,0},{40,0,0,0}};};
        if(!benchmark){
            reset(Scene::empty);configure_mission({});require(step({0,200,170,fire}),"accepted_shot_step");glFinish();poll();
            game::EventBatch shot_packet;std::array<float,8> projectile{};gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.BindBuffer(GL_COPY_READ_BUFFER,s.buffers[6]);gl.GetBufferSubData(GL_COPY_READ_BUFFER,event_offset,sizeof(shot_packet),&shot_packet);gl.GetBufferSubData(GL_COPY_READ_BUFFER,32+sizeof(MissionSnapshot),sizeof(projectile),projectile.data());s.invalidate_bindings();
            require(shot_packet.header[0]<=game::events_per_tick,"bounded_shot_packet");const auto end=shot_packet.events.begin()+shot_packet.header[0];const auto accepted=std::find_if(shot_packet.events.begin(),end,[](const auto& e){return e.kind==static_cast<unsigned>(game::EventKind::accepted)&&e.reserved==1;});
            require(accepted!=end&&projectile[4]>0&&accepted->payload[0]==projectile[5]&&accepted->payload[1]==projectile[6]&&accepted->extra[0]==projectile[2]&&accepted->extra[1]==projectile[3],"accepted_muzzle_matches_exact_projectile_spawn_before_recoil");
            // Independent radial-kernel and segment/AABB oracle for the point raster path.
            for(unsigned scene=0;scene<3;++scene){
                std::vector<Grain> input(3);const std::array<std::array<float,2>,3> positions{{{100.1F,80.8F},{101.7F,81.3F},{.2F,.7F}}};
                std::vector<unsigned> wall_map(width*height);if(scene==1)wall_map[81*width+101]=1;
                for(unsigned i=0;i<input.size();++i){auto& p=input[i];const auto at=positions[i];p.position={at[0],at[1],at[0],at[1]};p.contact={0,0,at[0],at[1]};p.motion={12.F+static_cast<float>(i),-4.F,1.F,i==1?6.F:5.F};}
                if(scene==2){input[0].position={319.8F,191.7F,319.8F,191.7F};input[0].contact={0,0,319.8F,191.7F};}
                load(input,wall_map);pool.set_options({VfxQuality::low,false});pool.begin_tick(s.renderer.terrain_image,s.epoch,1,1.F/120.F);s.renderer.build_vfx_field({s.buffers,s.current,true,false});
                std::vector<float> actual(width*height*4);glBindTexture(GL_TEXTURE_2D,pool.field_texture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,actual.data());s.invalidate_bindings();
                bool valid=true;for(int y=0;y<height;++y)for(int x=0;x<width;++x){std::array<double,4> expected{};
                    for(const auto& p:input){const double dx=x+.5-p.position[0],dy=y+.5-p.position[1],q=1-(dx*dx+dy*dy)/(1.35*1.35);if(q<=0)continue;bool blocked=false;
                        if(scene==1){double enter=0,leave=1;for(unsigned axis=0;axis<2;++axis){const double start=p.position[axis],delta=axis?dy:dx,minimum=axis?81:101,maximum=minimum+1;
                            if(std::abs(delta)<1e-12){if(start<minimum||start>maximum){enter=2;break;}}
                            else{double lo=(minimum-start)/delta,hi=(maximum-start)/delta;if(lo>hi)std::swap(lo,hi);enter=std::max(enter,lo);leave=std::min(leave,hi);}}
                            blocked=enter<=leave;}
                        if(blocked)continue;
                        const double w=q*q;expected[p.motion[3]==5?0:1]+=w;expected[2]+=w*p.motion[0];expected[3]+=w*p.motion[1];}
                    for(unsigned c=0;c<4;++c)if(std::abs(actual[(y*width+x)*4+c]-expected[c])>std::max(.002,std::abs(expected[c])*.003))valid=false;
                }
                require(valid,"point_field_matches_independent_kernel");pool.end_tick(0,{},false);
            }
            reset(Scene::sand_water);const auto before=bytes();const auto critical=event_stats();const auto physics_tick=metrics().steps;
            pool.set_options({VfxQuality::low,false});pool.reset(gen);require(check_pool(pool.snapshot()),"initial_partition");
            std::array<VfxEvent,3> events{event(VfxKind::explosion,1),event(VfxKind::impact,2),event(VfxKind::muzzle,3)};advance(events);auto first=pool.snapshot();require(first.counters.emitted[1]==1&&first.counters.emitted[3]==1&&first.counters.emitted[8]>40&&check_pool(first),"confirmed_effect_catalog");
            --tick;advance(events,0);const auto duplicate=pool.snapshot();require(first.counters.emitted==duplicate.counters.emitted,"duplicate_batch_not_reemitted");
            for(unsigned i=0;i<240;++i)advance();
            require(pool.snapshot().alive.empty(),"expired_slots_reclaimed");
            std::vector<VfxParticle> full(vfx_capacity,particle(VfxKind::explosion,60,60,0,0,1000));pool.seed_for_test(full);auto explosion=event(VfxKind::explosion);advance(std::span{&explosion,1});auto saturated=pool.snapshot();require(saturated.alive.size()==vfx_capacity&&saturated.counters.rejected[1]>0&&check_pool(saturated),"full_pool_nonblocking");
            for(auto& p:full)p.previous_age_life[3]=.001F;
            pool.seed_for_test(full);advance();require(pool.snapshot().free.size()==vfx_capacity&&check_pool(pool.snapshot()),"full_pool_recycled");
            std::fill(field.begin(),field.end(),0.F);std::fill(walls.begin(),walls.end(),std::uint16_t{0});for(unsigned y=0;y<192;++y)walls[y*320+100]=1;
            auto fast=particle(VfxKind::spark,99.5F,80.5F,1000,0);pool.seed_for_test(std::span{&fast,1});advance({},.1F);require(pool.snapshot().alive.empty(),"swept_one_cell_wall");
            std::fill(walls.begin(),walls.end(),std::uint16_t{0});walls[79*width+100]=1;
            auto corner=particle(VfxKind::jet,99.5F,79.5F,100,100);pool.seed_for_test(std::span{&corner,1});advance({},.02F);require(pool.snapshot().alive.empty(),"supercover_corner");
            walls[80*width+100]=1;auto mark=particle(VfxKind::scorch,100.5F,80.5F);pool.seed_for_test(std::span{&mark,1});advance();require(pool.snapshot().alive.size()==1,"scorch_requires_support");std::fill(walls.begin(),walls.end(),std::uint16_t{0});advance();require(pool.snapshot().alive.empty(),"scorch_removed_with_terrain");
            for(unsigned i=0;i<320*192;++i)field[4*i]=.5F;
            pool.reset(gen);for(unsigned i=0;i<60;++i)advance({},1.F/120.F,true);require(pool.snapshot().alive.empty(),"calm_water_no_emission");
            auto bubble=particle(VfxKind::bubble,60,60);pool.seed_for_test(std::span{&bubble,1});for(unsigned i=0;i<320*192;++i)field[4*i]=1;for(unsigned i=0;i<30;++i)advance();auto rising=pool.snapshot();require(rising.alive.size()==1&&rising.particles[rising.alive[0]].position_velocity[1]<60,"bubble_rises");
            for(unsigned i=0;i<320*192;++i){field[4*i]=0;field[4*i+1]=1;}advance();require(pool.snapshot().alive.empty(),"water_decorations_not_oil");
            std::fill(field.begin(),field.end(),0.F);for(unsigned y=0;y<192;++y)for(unsigned x=0;x<320;++x){const unsigned i=y*320+x;field[4*i]=(x%2)?.6F:.2F;field[4*i+2]=(x%2)?100.F:-100.F;}
            // Opposing streams and a density edge create a known nonzero source.
            for(unsigned y=0;y<192;++y)for(unsigned x=0;x<320;++x)field[4*(y*320+x)]=(x%3==0)?.7F:.2F;
            std::vector<VfxParticle> reserved(vfx_capacity-2460,particle(VfxKind::explosion,60,60));pool.seed_for_test(reserved);advance({},1.F/60.F,true);auto reserve=pool.snapshot();require(reserve.counters.counts[1]>=2458&&(reserve.counters.clock[2]>0||(reserve.counters.rejected[5]+reserve.counters.rejected[6]+reserve.counters.rejected[7])>0)&&check_pool(reserve),"low_priority_reserve");
            pool.reset(gen);auto old=event(VfxKind::explosion);advance(std::span{&old,1});pool.set_options({VfxQuality::off,false});require(pool.snapshot().alive.empty(),"off_clears_pool");pool.set_options({VfxQuality::low,false});advance();require(pool.snapshot().alive.empty(),"reenable_no_backlog");
            pool.reset(++gen);tick=0;require(pool.snapshot().alive.empty()&&pool.snapshot().counters.clock[0]==gen,"generation_reset");
            require(before==bytes()&&critical==event_stats()&&physics_tick==metrics().steps,"eight_physics_buffers_tick_and_events_unchanged");
            for(int scale:{3,5}){s.renderer.resize_for_test(scale);render(1);save_image(output/(scale==3?"vfx-empty-960.bmp":"vfx-empty-1600.bmp"));}
            require(before==bytes(),"render_preserves_physics");s.renderer.resize_for_test(3);
            for(unsigned quality=0;quality<3;++quality){reset_mission();pool.set_options({static_cast<VfxQuality>(quality),false});unsigned submitted=0;while(submitted<1700){if(step(scripted_mission_input(submitted)))++submitted;else glFinish();if(submitted%16==0){glFinish();poll();}}glFinish();poll();const auto result=read_mission();report<<"mission_"<<vfx_quality_name(static_cast<VfxQuality>(quality))<<"_score="<<result.inventory[2]<<"\n";require(result.inventory[2]==3&&result.status[3]!=0,"mission_completed");}
            pool.set_options({VfxQuality::low,false});reset_mission();for(unsigned i=0;i<180;++i){while(!step(scripted_mission_input(i)))glFinish();if(i%16==0){glFinish();poll();}}render(1);save_image(output/"mission-vfx.bmp");
            validate_vfx_replay(output,s.execution);report<<"paired_physics_replay=PASS\n";
            require(pool.timing_query_objects()==0,"disabled_vfx_timers_allocate_no_queries");
            require(glGetError()==GL_NO_ERROR,"opengl_no_error");report<<"result=PASS\n";return {};
        }
        return run_vfx_benchmark(output);
    }catch(const std::exception& e){std::ofstream{output/"vfx-error.txt"}<<e.what()<<'\n';return std::unexpected{e.what()};}
}
