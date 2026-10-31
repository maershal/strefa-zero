std::expected<void,std::string> ParticleWorld::validate_renderer(const std::filesystem::path& output,bool benchmark){
    try{
        std::filesystem::create_directories(output);auto& s=*impl_;auto& renderer=s.renderer;auto& gl=s.gl;
        auto require=[](bool valid,const char* message){if(!valid)throw std::runtime_error{message};};
        std::ofstream report{output/"render-tests.txt"},metadata{output/"render-environment.txt"};
        metadata<<"renderer="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nversion="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<"\nphysics=I07.1 legacy unless explicitly configured\n";
        for(const auto& pass:detail::scene_passes)metadata<<pass.name<<" | "<<pass.inputs<<" -> "<<pass.output<<" | "<<pass.format<<" | "<<pass.state<<" | "<<pass.barrier<<'\n';
        auto physical_bytes=[&]{std::vector<std::byte> result;gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            for(auto id:s.buffers){gl.BindBuffer(GL_COPY_READ_BUFFER,id);GLint size{};gl.GetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);
                const auto at=result.size();result.resize(at+static_cast<std::size_t>(size));gl.GetBufferSubData(GL_COPY_READ_BUFFER,0,size,result.data()+at);}s.invalidate_bindings();return result;};
        auto drain=[&]{glFinish();poll(false);renderer.poll_samples();};
        auto input=[&]{return detail::RenderState{s.buffers,s.current,s.fluids_enabled,s.mission_enabled};};
        std::ofstream timing{output/"render-samples.csv"};timing<<"block,order,scene,scale,variant,frame";for(auto name:render_pass_names)timing<<','<<name<<"_ms";timing<<",water_ms,cpu_submit_ms,memory_bytes,terrain_draw_ms\n";
        auto write_samples=[&](unsigned block,unsigned order,std::string_view scene,int scale,std::string_view variant,const std::vector<double>& cpu){
            drain();const auto samples=renderer.take_samples();require(samples.size()==cpu.size(),"Missing per-pass timestamps");
            for(std::size_t i=0;i<samples.size();++i){const auto& x=samples[i];timing<<block<<','<<order<<','<<scene<<','<<scale<<','<<variant<<','<<x.frame;
                for(double ms:x.milliseconds){require(std::isfinite(ms)&&ms>=0,"Invalid render timestamp");timing<<','<<ms;}
                const double water=x.milliseconds[1]+x.milliseconds[2]+x.milliseconds[3]+x.milliseconds[4]+x.milliseconds[5];
                timing<<','<<water<<','<<cpu[i]<<','<<renderer.memory_bytes()<<','<<x.terrain_ms<<'\n';}};
        const auto original_options=render_options();
        if(benchmark){
            // Each ABBA block reuses one exact GPU snapshot. CPU readback/capture
            // and all physics are outside the measured render loop.
            for(int scale:{3,5})for(unsigned scene=0;scene<3;++scene){
                renderer.resize_for_test(scale);
                if(scene==0)reset_mission();else reset(scene==1?Scene::sand_water:Scene::oil_water);
                const auto frozen=renderer.freeze(input());
                const std::array<std::string_view,3> names{"mission","sand_water","oil_water"};
                const std::array<RenderQuality,3> qualities{RenderQuality::basic,RenderQuality::standard,RenderQuality::high};
                for(unsigned block=1;block<=3;++block)for(unsigned quality=0;quality<3;++quality)for(unsigned order=0;order<4;++order){
                    const bool candidate=order==1||order==2;renderer.stage=candidate?RenderStage::complete:RenderStage::reference;
                    renderer.set_options({qualities[quality],false});for(unsigned n=0;n<120;++n){renderer.render(frozen,1);if(n%16==15)glFinish();}drain();renderer.reset_profiles();
                    renderer.set_options({qualities[quality],true});std::vector<double> cpu;
                    for(unsigned n=0;n<240;++n){const auto began=SDL_GetTicksNS();renderer.render(frozen,1);cpu.push_back(static_cast<double>(SDL_GetTicksNS()-began)/1e6);if(n%16==15)glFinish();}
                    std::string variant=candidate?std::string{render_quality_name(qualities[quality])}:"reference_for_"+std::string{render_quality_name(qualities[quality])};
                    write_samples(block,order,names[scene],scale,variant,cpu);
                }renderer.release_frozen();
            }
            renderer.resize_for_test(3);renderer.set_options({RenderQuality::basic,true});reset(Scene::empty);
            renderer.reset_profiles();std::ofstream terrain_csv{output/"terrain-update.csv"};terrain_csv<<"sample,full_dirty_preprocess_ms\n";
            for(unsigned n=0;n<120;++n){paint({40.F+static_cast<float>(n%20),40,40.F+static_cast<float>(n%20),40,2,n%2?Material::erase:Material::wall});if(n%16==15)drain();}
            drain();const auto terrain_samples=renderer.take_terrain_samples();require(!terrain_samples.empty(),"No terrain-update timestamps");
            for(std::size_t n=0;n<terrain_samples.size();++n)terrain_csv<<n<<','<<terrain_samples[n]<<'\n';
            renderer.stage=RenderStage::complete;renderer.set_options(original_options);renderer.resize_for_test(3);
            report<<"benchmark=passed (3 ABBA blocks, 3 qualities, 3 scenes, 2 extents; 240 samples per process segment)\n";return {};
        }
        const auto probe= s.shader_library.program({{GL_COMPUTE_SHADER,s.shader_folder/"render_probe.comp"}});
        const auto probe_count=gl.GetUniformLocation(probe,"probeCount");
        unsigned checkpoints=0,probes_checked=0;
        std::ofstream filters{output/"filter-comparison.csv"};filters<<"scene,spatial_phase_error,bilateral_phase_error\n";
        auto check_scene=[&](std::string_view name,bool check_support){
            drain();require(glGetError()==GL_NO_ERROR,"GL error before render fixture");const auto before=physical_bytes();const auto events=event_stats();const auto tick=s.metrics.steps;const auto frozen=renderer.freeze(input());
            std::vector<float> raw;std::vector<std::uint8_t> opaque;
            for(unsigned pass=0;pass<5;++pass){renderer.set_options({pass==2?RenderQuality::basic:pass==3?RenderQuality::standard:RenderQuality::high,false});
                renderer.stage=pass==0?RenderStage::reference:pass==1?RenderStage::opaque:pass==4?RenderStage::complete:RenderStage::optics;
                // Emulate foreign state, including an FBO and viewport, before every draw.
                glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
                s.invalidate_bindings();renderer.render(frozen,.5F);const auto image=renderer.read_image();
                require(image.size()==960U*576U*4U,"Wrong output extent");for(std::size_t p=3;p<image.size();p+=4)require(image[p]==255,"Nonopaque scene output");
                if(pass==1)opaque=image;
                if(pass==2)raw=renderer.read_field();
                if(pass==4&&check_support){unsigned visible=0;
                    for(std::size_t i=0;i<raw.size()/4;++i)if(raw[i*4]+raw[i*4+1]>.35F){
                        const auto base=i*4;const auto delta=std::abs(int(image[base])-int(opaque[base]))+std::abs(int(image[base+1])-int(opaque[base+1]))+std::abs(int(image[base+2])-int(opaque[base+2]));if(delta>2)++visible;}
                    require(visible>0,"Drop or stream disappeared after composition");}
                const std::array<std::string_view,5> suffix{"reference","A","basic","standard","C-high"};
                require(glGetError()==GL_NO_ERROR,"GL error in render/capture");
                renderer.save_image(output/(std::string{name}+"-"+std::string{suffix[pass]}+".bmp"));
            }
            const auto bilateral_field=renderer.read_field(1);renderer.bilateral=false;renderer.render(frozen,.5F);
            const auto spatial_field=renderer.read_field(1);renderer.save_image(output/(std::string{name}+"-spatial.bmp"));renderer.bilateral=true;
            double spatial_error=0,bilateral_error=0;unsigned phase_pixels=0;
            for(std::size_t i=0;i<raw.size();i+=4){const float d=raw[i]+raw[i+1];if(d<.35F)continue;
                const double phase=raw[i+1]/d,spatial_d=spatial_field[i]+spatial_field[i+1],bilateral_d=bilateral_field[i]+bilateral_field[i+1];
                if(spatial_d>.01&&bilateral_d>.01){spatial_error+=std::abs(spatial_field[i+1]/spatial_d-phase);bilateral_error+=std::abs(bilateral_field[i+1]/bilateral_d-phase);++phase_pixels;}}
            filters<<name<<','<<spatial_error/std::max(1U,phase_pixels)<<','<<bilateral_error/std::max(1U,phase_pixels)<<'\n';
            require(bilateral_error<=spatial_error+.00001,"Bilateral increased phase-boundary error");
            const auto terrain_texture=renderer.read_terrain_texture();const auto authoritative_terrain=read_terrain();
            require(std::equal(terrain_texture.begin(),terrain_texture.end(),authoritative_terrain.begin()),"Terrain texture differs from authoritative GPU map");
            require(physical_bytes()==before&&s.metrics.steps==tick&&event_stats()==events,"Rendering changed authoritative buffers/tick/events");
            renderer.release_frozen();++checkpoints;
            report<<"scene_"<<name<<"=passed (five variants, exact frozen GPU buffers, physical buffers unchanged)\n";
        };
        for(const auto& pair:std::array<std::pair<Scene,const char*>,4>{{{Scene::water_tank,"tank"},{Scene::dam_break,"dam"},{Scene::oil_water,"phases"},{Scene::sand_water,"sand"}}}){reset(pair.first);check_scene(pair.second,true);}
        auto grain=[](float x,float y,unsigned kind,float vx=12.F,float vy=-4.F){Grain g;g.position={x,y,x,y};g.contact={0,0,x,y};g.motion={vx,vy,1,static_cast<float>(kind)};return g;};
        for(unsigned scenario=0;scenario<7;++scenario){std::vector<Grain> particles;std::vector<std::uint32_t> walls(width*height);
            if(scenario==0)particles.push_back(grain(100.5F,50.5F,5));
            if(scenario==1)for(int y=48;y<70;++y)particles.push_back(grain(100.5F,y+.5F,5));
            if(scenario==2){for(int y=45;y<85;++y){walls[static_cast<std::size_t>(y)*width+100]=1;particles.push_back(grain(99.25F,y+.5F,5));particles.push_back(grain(101.5F,y+.5F,6,-8,2));}}
            if(scenario==3){walls[80*width+100]=1;walls[79*width+101]=1;particles={grain(100.5F,79.5F,5),grain(101.5F,80.5F,6)};}
            if(scenario==4)for(int y=45;y<75;++y)if(y<55||y>=65)for(int x=80;x<88;++x)particles.push_back(grain(x+.5F,y+.5F,5));
            if(scenario==5)particles={grain(.5F,.5F,5),grain(319.5F,191.5F,6)};
            if(scenario==6){for(int y=60;y<100;++y)for(int x=90;x<125;++x)particles.push_back(grain(x+.5F,y+.5F,x<107?5U:6U));particles.push_back(grain(100.5F,80.5F,1));}
            const std::array<std::string_view,7> names{"drop","stream","thin-wall","corner","segments","edges","immersion"};
            load(particles,walls);if(scenario==6){MissionConfig config;config.spawn_x=100;config.spawn_y=86;configure_mission(config);}
            check_scene(names[scenario],true);renderer.stage=RenderStage::complete;renderer.set_options({RenderQuality::high,false});renderer.render(input(),1);
            const auto raw=renderer.read_field();const auto depth=renderer.read_field(2);
            for(float value:raw)require(std::isfinite(value),"Nonfinite splat field");
            for(float value:depth)require(std::isfinite(value)&&value>=0&&value<=32.01F,"Invalid optical depth");
            for(int y=0;y<576;++y)for(int x=0;x<960;++x)if(walls[static_cast<std::size_t>(y/3)*width+x/3]){
                const auto i=(static_cast<std::size_t>(y)*960+x)*4;require(raw[i]==0&&raw[i+1]==0,"Splat crossed solid cell");}
            if(scenario==0||scenario==1){ // independent analytic splat oracle
                for(int y=140;y<216;++y)for(int x=295;x<309;++x){const double wx=(x+.5)/3.,wy=(y+.5)/3.;double expected=0;
                    for(const auto& g:particles){const double dx=(wx-g.position[0])/1.35,dy=(wy-g.position[1])/1.35;const double q=std::max(0.,1.-dx*dx-dy*dy);expected+=q*q;}
                    const auto i=(static_cast<std::size_t>(y)*960+x)*4;require(std::abs(raw[i]-expected)<.02,"Splat density disagrees with analytic CPU oracle");
                    if(raw[i]>.1F){require(std::abs(raw[i+2]/raw[i]-12)<.08F&&std::abs(raw[i+3]/raw[i]+4)<.03F,"Velocity was not normalized by splat weight");}}
            }
            if(scenario==4){const auto d1=depth[50*3*960+83*3],d2=depth[70*3*960+83*3];require(std::abs(d1-d2)<.1F,"Disconnected columns accumulate optical depth");}
            if(scenario==2||scenario==3){
                std::array<std::array<float,4>,256> rays{};std::array<std::array<unsigned,4>,256> result{};
                for(unsigned i=0;i<256;++i){const float y=scenario==2?44.15F+static_cast<float>(i%40):78.15F+static_cast<float>(i%4);rays[i]={98.15F+static_cast<float>(i%3),y,102.75F,y+static_cast<float>(int(i%5)-2)*.5F};}
                for(unsigned i=0;i<256;i+=4){const float y=scenario==2?50.5F+static_cast<float>(i%25):79.5F;rays[i]={99.1F,y,99.6F,y+.15F};}
                GLuint buffer{};gl.GenBuffers(1,&buffer);gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);gl.BufferData(GL_SHADER_STORAGE_BUFFER,sizeof(rays)+sizeof(result),nullptr,GL_STATIC_COPY);
                gl.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(rays),rays.data());gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,7,buffer);gl.UseProgram(probe);gl.Uniform1i(probe_count,256);
                // Bind the exact raw splat field used for production ray validation.
                renderer.bind_probe_field();gl.DispatchCompute(4,1,1);gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,sizeof(rays),sizeof(result),result.data());
                for(unsigned i=0;i<256;++i){const auto& ray=rays[i];bool clear=true;
                    // Independent closed-rectangle slab intersection, not the shader DDA.
                    for(int y=43;y<87&&clear;++y)for(int x=97;x<105&&clear;++x)if(walls[static_cast<std::size_t>(y)*width+x]){
                        double lo=0,hi=1;bool hit=true;for(int axis=0;axis<2;++axis){const double a=ray[axis],d=ray[axis+2]-a,b=axis==0?x:y;
                            if(std::abs(d)<1e-9){if(a<b||a>b+1)hit=false;}else{const double u=(b-a)/d,v=(b+1-a)/d;lo=std::max(lo,std::min(u,v));hi=std::min(hi,std::max(u,v));}}
                        if(hit&&lo<=hi)clear=false;}
                    require(result[i][0]==static_cast<unsigned>(clear),"Terrain ray disagrees with independent rectangle oracle");++probes_checked;
                    const int ax=std::clamp(static_cast<int>(ray[0]*3),0,959),ay=std::clamp(static_cast<int>(ray[1]*3),0,575),bx=std::clamp(static_cast<int>(ray[2]*3),0,959),by=std::clamp(static_cast<int>(ray[3]*3),0,575);
                    const auto at=(static_cast<std::size_t>(ay)*960+ax)*4;const bool oil=raw[at+1]>raw[at];bool fluid_clear=true;
                    for(int py=std::min(ay,by);py<=std::max(ay,by)&&fluid_clear;++py)for(int px=std::min(ax,bx);px<=std::max(ax,bx)&&fluid_clear;++px){
                        double lo=0,hi=1;bool hit=true;for(int axis=0;axis<2;++axis){const double a=axis==0?ax+.5:ay+.5,d=axis==0?bx-ax:by-ay,b=axis==0?px:py;
                            if(d==0){if(a<b||a>b+1)hit=false;}else{const double u=(b-a)/d,v=(b+1-a)/d;lo=std::max(lo,std::min(u,v));hi=std::min(hi,std::max(u,v));}}
                        if(hit&&lo<=hi){const auto index=(static_cast<std::size_t>(py)*960+px)*4;if(raw[index]+raw[index+1]<.12F||(raw[index+1]>raw[index])!=oil)fluid_clear=false;}
                    }
                    require(result[i][1]==static_cast<unsigned>(fluid_clear),"Liquid ray disagrees with independent pixel-rectangle oracle");
                }
                gl.DeleteBuffers(1,&buffer);s.invalidate_bindings();
            }
        }
        reset_mission();check_scene("mission",false);
        render(1);require(renderer.targets.hit[0]==0,"Reset showed a hit flash");
        static_cast<void>(s.gameplay.prepare({}));game::EventBatch confirmed;confirmed.header={1,1,s.epoch,0};
        confirmed.events[0].generation=s.epoch;confirmed.events[0].tick=1;confirmed.events[0].id=1;
        confirmed.events[0].kind=static_cast<unsigned>(game::EventKind::hit);confirmed.events[0].subject=0;confirmed.events[0].value=1;
        s.gameplay.consume(confirmed);render(1);require(renderer.targets.hp[0]==2&&renderer.targets.hit[0]>0,"Confirmed hit did not reach target presentation");
        const auto hit_started=s.hit_started[0];s.gameplay.consume(confirmed);render(1);require(s.hit_started[0]==hit_started&&renderer.targets.hp[0]==2,"Duplicate event restarted hit feedback");
        reset_mission();render(1);require(renderer.targets.hp[0]==3&&renderer.targets.hit[0]==0,"Reset retained old target feedback");
        report<<"confirmed_hit_deduplication_and_reset=passed\n";
        renderer.resize_for_test(5);renderer.render(input(),1);require(renderer.read_image().size()==1600U*960U*4U,"Large render extent incorrect");renderer.save_image(output/"mission-1600.bmp");renderer.resize_for_test(3);
        renderer.stage=RenderStage::complete;renderer.set_options(original_options);s.invalidate_bindings();
        require(glGetError()==GL_NO_ERROR,"GL error after renderer validation");
        report<<"frozen_checkpoints="<<checkpoints<<"\nterrain_rays="<<probes_checked<<"\nanalytic_splat_and_velocity=passed\nsegmented_depth=passed\nforeign_state_and_capture=passed\nlarge_extent=passed\nresult=passed\n";
        return {};
    }catch(const std::exception& error){std::ofstream{output/"render-failure.txt"}<<error.what();return std::unexpected{error.what()};}
}
