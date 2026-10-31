#pragma once
#include "game/session.hpp"
#include "gpu/render_device.hpp"
#include <algorithm>
#include <expected>
namespace strefa::app {
std::expected<void,std::string> validate_cpu_vfx(gpu::RenderDevice&,const std::filesystem::path&);
// CPU reference data is already authoritative on the host. No GPU-world readback.
class CpuVfxAdapter {
public:
    void reset(gpu::RenderDevice& device){++generation_;device.vfx.reset(generation_);}
    void tick(gpu::RenderDevice& device,const game::GameSession& game){
        auto& pool=device.vfx;if(!pool.enabled())return;
        pool.begin_tick(0,generation_,static_cast<unsigned>(game.ticks()),game::GameSession::tick_seconds);
        for(int y=0;y<sim::World::height;++y)for(int x=0;x<sim::World::width;++x){const sim::Point p{x,y};const auto value=game.world().sample_fluid(p);const auto i=static_cast<unsigned>(y*sim::World::width+x);
            walls_[i]=sim::blocks_actor(value.material)?1:0;const float water=value.water,oil=value.oil;
            field_[4*i]=water;field_[4*i+1]=oil;field_[4*i+2]=(water+oil)*value.velocity.x;field_[4*i+3]=(water+oil)*value.velocity.y;
        }
        pool.upload_field(field_,walls_);events_.clear();
        for(const auto& e:game.visual_events()){const auto kind=e.kind==game::EffectKind::blast?gpu::VfxKind::explosion:e.kind==game::EffectKind::impact?gpu::VfxKind::impact:gpu::VfxKind::muzzle;
            events_.push_back({{generation_,static_cast<unsigned>(game.ticks()),static_cast<unsigned>(e.id),static_cast<unsigned>(kind)},{e.position.x,e.position.y,kind==gpu::VfxKind::explosion?9.F:1.F,0},{e.velocity.x,e.velocity.y,0,0}});}
        if(game.player().jetting){const auto& p=game.player();events_.push_back({{generation_,static_cast<unsigned>(game.ticks()),0,static_cast<unsigned>(gpu::VfxKind::jet)},{p.feet.x+(p.aim.x<0?3.F:-3.F),p.feet.y-8,1,0},{p.velocity.x*.2F,p.velocity.y*.2F,0,0}});}
        if(events_.size()>gpu::vfx_event_capacity)std::stable_sort(events_.begin(),events_.end(),[](const auto& a,const auto& b){return (a.identity[3]==static_cast<unsigned>(gpu::VfxKind::impact))<(b.identity[3]==static_cast<unsigned>(gpu::VfxKind::impact));});
        pool.end_tick(0,events_);
    }
private:
    unsigned generation_{0x80000000u};
    std::vector<float> field_=std::vector<float>(320*192*4);
    std::vector<std::uint16_t> walls_=std::vector<std::uint16_t>(320*192);
    std::vector<gpu::VfxEvent> events_;
};
}
