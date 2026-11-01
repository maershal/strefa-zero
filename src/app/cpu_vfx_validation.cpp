#include "app/cpu_vfx.hpp"
#include "platform/texture.hpp"
#include "sim/fixed_clock.hpp"
#include <fstream>
#include <chrono>
namespace strefa::app {
std::expected<void,std::string> validate_cpu_vfx(gpu::RenderDevice& device,const std::filesystem::path& output){
    try{
        std::ofstream report{output/"cpu-vfx-tests.txt"},timing{output/"cpu-vfx-samples.csv"};timing<<"fps,tick,cpu_adapter_ms,field_ms,update_ms,emit_ms,draw_ms\n";
        std::array<unsigned,gpu::vfx_kinds> reference{};auto& pool=device.vfx;
        sim::PhysicsConfig config;config.thermal=config.fluids=config.air=config.reactions=config.motion=false;
        for(unsigned fps:{30u,60u,144u}){
            game::GameSession game{{},{},config};CpuVfxAdapter adapter;pool.set_options({gpu::VfxQuality::low,true});adapter.reset(device);sim::FixedClock clock;
            std::vector<sim::Color> pixels(sim::World::cell_count);game.world().render(std::span<sim::Color,sim::World::cell_count>{pixels.data(),pixels.size()},true);
            auto canvas=platform::Texture::create_rgba(320,192,std::as_bytes(std::span{pixels}));if(!canvas)return std::unexpected{canvas.error()};
            while(game.ticks()<120){const int steps=clock.advance(1.0/fps,false,1);for(int i=0;i<steps&&game.ticks()<120;++i){game::TickInput input;input.fire=true;input.jet=game.ticks()<30;input.aim={200,170};game.tick(input);const auto before=game.checksum();const auto start=std::chrono::steady_clock::now();adapter.tick(device,game);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(before!=game.checksum())throw std::runtime_error{"CPU VFX changed authoritative reference state"};
                timing<<fps<<','<<game.ticks()<<','<<ms<<",0,0,0,0\n";if(game.ticks()%8==0){glFinish();pool.poll();}
            }}
            pool.cpu_canvas(canvas->id(),canvas->id(),1);glFinish();pool.poll();const auto snap=pool.snapshot();if(fps==30)reference=snap.counters.emitted;else if(snap.counters.emitted!=reference)throw std::runtime_error{"CPU VFX emission depends on frame rate"};
            if(!snap.counters.emitted[3]||!snap.counters.emitted[4])throw std::runtime_error{"CPU muzzle/jet emissions missing"};
            report<<"fps_"<<fps<<"=PASS; authoritative checksum unchanged, muzzle="<<snap.counters.emitted[3]<<", jet="<<snap.counters.emitted[4]<<'\n';
            for(const auto& sample:pool.take_samples())timing<<fps<<','<<sample.tick<<",0,"<<sample.field_ms<<','<<sample.update_ms<<','<<sample.emit_ms<<','<<sample.draw_ms<<'\n';
        }
        pool.set_options({gpu::VfxQuality::off,false});if(!pool.snapshot().alive.empty())throw std::runtime_error{"CPU VFX off retained particles"};pool.set_options({gpu::VfxQuality::low,false});
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error{"CPU VFX OpenGL error"};
        report<<"result=PASS\n";return {};
    }catch(const std::exception& e){std::ofstream{output/"cpu-vfx-error.txt"}<<e.what()<<'\n';return std::unexpected{e.what()};}
}
}
