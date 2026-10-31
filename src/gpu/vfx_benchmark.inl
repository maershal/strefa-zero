std::expected<void,std::string> ParticleWorld::run_vfx_benchmark(const std::filesystem::path& output){
    auto& s=*impl_;auto& pool=s.resources->vfx;auto& gl=s.gl;std::ofstream csv{output/"vfx-samples.csv"};csv<<std::setprecision(12)<<"scene,scale,quality,block,order,variant,frame,total_gpu_ms,update_gpu_ms,render_gpu_ms,cpu_submit_ms,alive,free,drawn,memory_bytes\n";
    std::ofstream counts{output/"vfx-pool-segments.csv"};counts<<"scene,scale,quality,block,order,variant,kind,emitted,rejected,budget_limited_passes,malformed\n";
    std::ofstream passes{output/"vfx-pass-samples.csv"};passes<<"scene,tick,field_ms,update_ms,emit_ms,draw_ms\n";
    for(int scale:{3,5})for(unsigned scene=0;scene<4;++scene){
        s.renderer.resize_for_test(scale);
        if(scene==0)reset_mission();else if(scene==1)reset(Scene::dam_break);else if(scene==2)reset(Scene::sand_water);else{
            std::vector<Grain> water;std::vector<unsigned> walls(width*height);for(int y=80;y<186;++y)for(int x=20;x<300;++x){Grain g;g.position={x+.5F,y+.5F,x+.5F,y+.5F};g.contact={0,0,x+.5F,y+.5F};g.motion={0,0,1,5};water.push_back(g);}load(water,walls);
        }
        pool.set_options({VfxQuality::off,false});
        if(scene==1||scene==2)for(unsigned warmup=0;warmup<120;++warmup){while(!step())glFinish();if(warmup%16==15){glFinish();poll();}}
        const auto input=s.renderer.freeze({s.buffers,s.current,s.fluids_enabled,s.mission_enabled});
        const std::array<std::string_view,4> names{"mission","dam_break","sand_water","saturated"};
        std::vector<VfxParticle> full(vfx_capacity);for(unsigned i=0;i<vfx_capacity;++i){const float x=30.5F+static_cast<float>(i%260),y=110.5F+static_cast<float>(i/260);auto& p=full[i];p.position_velocity={x,y,0,0};p.previous_age_life={x,y,0,100000};p.appearance={7,0xe6e9f7f0u,std::bit_cast<unsigned>(.35F),i};}
        for(unsigned quality=0;quality<3;++quality)for(unsigned block=1;block<=3;++block)for(unsigned order=0;order<4;++order){
            const bool candidate=order==1||order==2;const auto mode=candidate?static_cast<VfxQuality>(quality):VfxQuality::off;
            pool.set_options({mode,false});pool.reset(s.epoch);if(scene==3&&mode!=VfxQuality::off)pool.seed_for_test(full);
            unsigned tick=0;
            const auto update_vfx=[&]{if(!pool.enabled())return;pool.begin_tick(s.renderer.terrain_image,s.epoch,++tick,1.F/120.F);s.renderer.build_vfx_field(input);
                std::array<VfxEvent,2> events{};unsigned count=0;if(scene==0&&tick%12==0)events[count++]={{s.epoch,tick,tick,3},{40,172,1,0},{600,0,0,0}};if(scene==0&&tick%120==0)events[count++]={{s.epoch,tick,tick+1,1},{92,160,20,0},{0,0,0,0}};
                pool.end_tick(0,std::span{events.data(),count},s.fluids_enabled);s.invalidate_bindings();};
            for(unsigned frame=0;frame<120;++frame){update_vfx();update_vfx();s.renderer.render(input,1);if(frame%16==15){glFinish();pool.poll();}}
            glFinish();pool.poll();
            struct Frame {std::array<GLuint,3> q{};double cpu{};};std::array<Frame,240> frames{};
            for(auto& frame:frames)gl.GenQueries(3,frame.q.data());
            for(unsigned frame=0;frame<frames.size();++frame){auto& row=frames[frame];const auto began=SDL_GetTicksNS();gl.QueryCounter(row.q[0],GL_TIMESTAMP);update_vfx();update_vfx();gl.QueryCounter(row.q[1],GL_TIMESTAMP);s.renderer.render(input,1);gl.QueryCounter(row.q[2],GL_TIMESTAMP);row.cpu=static_cast<double>(SDL_GetTicksNS()-began)/1e6;if(frame%16==15){glFinish();pool.poll();}}
            glFinish();pool.poll();const auto counters=pool.snapshot().counters;
            if(scene==3&&mode!=VfxQuality::off&&counters.counts[0]!=vfx_capacity)throw std::runtime_error{"Saturated benchmark lost live particles"};
            for(unsigned frame=0;frame<frames.size();++frame){const auto& row=frames[frame];std::array<GLuint64,3> t{};for(unsigned i=0;i<3;++i)gl.GetQueryObjectui64v(row.q[i],GL_QUERY_RESULT,&t[i]);csv<<names[scene]<<','<<scale<<','<<vfx_quality_name(static_cast<VfxQuality>(quality))<<','<<block<<','<<order<<','<<vfx_quality_name(mode)<<','<<frame<<','<<static_cast<double>(t[2]-t[0])/1e6<<','<<static_cast<double>(t[1]-t[0])/1e6<<','<<static_cast<double>(t[2]-t[1])/1e6<<','<<row.cpu<<','<<counters.counts[0]<<','<<counters.counts[1]<<','<<(counters.layers[0]+counters.layers[1]+counters.layers[2])<<','<<pool.memory_bytes()<<'\n';gl.DeleteQueries(3,row.q.data());}
            for(unsigned kind=0;kind<vfx_kinds;++kind)counts<<names[scene]<<','<<scale<<','<<vfx_quality_name(static_cast<VfxQuality>(quality))<<','<<block<<','<<order<<','<<vfx_quality_name(mode)<<','<<vfx_kind_names[kind]<<','<<counters.emitted[kind]<<','<<counters.rejected[kind]<<','<<counters.clock[2]<<','<<counters.clock[3]<<'\n';
            csv.flush();if(block==1&&order==1)s.renderer.save_image(output/(std::string{names[scene]}+"-"+std::to_string(scale)+"-"+std::string{vfx_quality_name(mode)}+".bmp"));
        }
        if(scale==3){pool.set_options({VfxQuality::low,true});pool.reset(s.epoch);if(scene==3)pool.seed_for_test(full);for(unsigned tick=1;tick<=120;++tick){pool.begin_tick(s.renderer.terrain_image,s.epoch,tick,1.F/120.F);s.renderer.build_vfx_field(input);pool.end_tick(0,{},s.fluids_enabled);if(tick%2==0)s.renderer.render(input,1);if(tick%8==0){glFinish();pool.poll();}}glFinish();for(const auto& sample:pool.take_samples())passes<<names[scene]<<','<<sample.tick<<','<<sample.field_ms<<','<<sample.update_ms<<','<<sample.emit_ms<<','<<sample.draw_ms<<'\n';}
        s.renderer.release_frozen();
    }
    pool.set_options({VfxQuality::low,false});s.renderer.resize_for_test(3);if(glGetError()!=GL_NO_ERROR)return std::unexpected{"VFX benchmark OpenGL error"};return {};
}
