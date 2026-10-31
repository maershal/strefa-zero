#include "game/gameplay_session.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace strefa::game {
bool CommandRing::reserve(const WorldCommand& c){
    if(!c.id||pending_==entries_.size())return false;
    for(auto& entry:entries_)if(!entry.id){entry=c;++pending_;return true;}
    return false;
}
bool CommandRing::acknowledge(unsigned id,bool accepted,GameplayState& state){
    for(auto& c:entries_)if(c.id==id&&id){
        if(!accepted){
            if(c.kind==static_cast<unsigned>(CommandKind::rifle))state.ammunition=std::min(30U,state.ammunition+1);
            if(c.kind==static_cast<unsigned>(CommandKind::grenade))state.grenades=std::min(3U,state.grenades+1);
        }else if(c.kind==static_cast<unsigned>(CommandKind::rifle))++state.shots;
        c={};--pending_;return true;
    }
    return false;
}
void GameplaySession::reset(unsigned generation,std::array<unsigned,3> hp,unsigned tick){
    *this=GameplaySession{};generation_=generation;tick_=acknowledged_tick_=tick;state_.hp=hp;
    state_.score=static_cast<unsigned>(std::ranges::count(hp,0U));
}
float GameplaySession::reload_remaining()const{return reload_end_>tick_?static_cast<float>(reload_end_-tick_)/120.F:0.F;}
CommandBatch GameplaySession::prepare(GameplayInput input){
    if(tick_>=std::numeric_limits<unsigned>::max()-256||next_id_>=std::numeric_limits<unsigned>::max()-4)
        throw std::overflow_error{"Gameplay counter exhausted: reset the world generation."};
    ++tick_;
    if(reload_end_&&tick_>=reload_end_){state_.ammunition=30;reload_end_=0;}
    // Preserve the effective 0.7/I05 timing: repeated float subtraction in GLSL
    // expired after 13 / 181 ticks. Quantize once; do not change weapon tempo.
    if(!state_.finished&&(input.buttons&8U)&&state_.ammunition<30&&!reload_end_)reload_end_=tick_+181;
    CommandBatch batch;batch.header={generation_,tick_,0,cancel_jump_?1U:0U};cancel_jump_=false;
    const auto add=[&](CommandKind kind){
        WorldCommand c;c.generation=generation_;c.tick=tick_;c.id=next_id_;c.kind=static_cast<unsigned>(kind);
        c.payload={input.aim_x,input.aim_y,kind==CommandKind::rifle?600.F:90.F,0};
        if(kind==CommandKind::jump)c.flags=tick_+12; // 100 ms buffer, inclusive execution tick
        if(!commands_.reserve(c))return false;
        ++next_id_;batch.commands[batch.header[2]++]=c;return true;
    };
    if(!state_.finished){
        if((input.buttons&4U)&&state_.ammunition&&!reload_end_&&tick_>=rifle_ready_&&add(CommandKind::rifle)){
            --state_.ammunition;rifle_ready_=tick_+13;
        }
        if((input.buttons&16U)&&state_.grenades&&tick_>=grenade_ready_&&add(CommandKind::grenade)){
            --state_.grenades;grenade_ready_=tick_+30; // shared 250 ms throw cooldown
        }
        if(input.buttons&1U)add(CommandKind::jump);
    }
    batch.inventory={state_.ammunition,state_.grenades,state_.score,state_.shots};
    batch.rules={rifle_ready_>tick_?static_cast<float>(rifle_ready_-tick_)/120.F:0.F,reload_remaining(),state_.finished?1.F:0.F,0};
    batch.targets={state_.hp[0],state_.hp[1],state_.hp[2],0};return batch;
}
void GameplaySession::consume(const EventBatch& batch){
    if(batch.header[2]!=generation_)return;
    const auto count=batch.header[0],tick=batch.header[1];
    if(tick<=acknowledged_tick_){++duplicate_batches_;return;}
    if(tick!=acknowledged_tick_+1||tick>tick_||count>events_per_tick||batch.header[3])
        throw std::runtime_error{"Incomplete gameplay event stream"};
    // Validate a complete packet before applying anything; IDs are local ordinals.
    for(unsigned i=0;i<count;++i){const auto& e=batch.events[i];
        if(e.generation!=generation_||e.tick!=tick||e.id!=i+1||e.kind<1||e.kind>7)
            throw std::runtime_error{"Invalid gameplay event ABI/order"};
    }
    for(unsigned i=0;i<count;++i){const auto& e=batch.events[i];
        switch(static_cast<EventKind>(e.kind)){
        case EventKind::accepted:case EventKind::rejected:
            commands_.acknowledge(e.command,e.kind==static_cast<unsigned>(EventKind::accepted),state_);break;
        case EventKind::hit:
            if(e.subject<state_.hp.size()){
                auto& hp=state_.hp[e.subject];const auto before=hp;hp-=std::min(hp,e.value);
                if(before&&hp==0)++state_.score;
            }break;
        case EventKind::explosion:++state_.explosions;break;
        case EventKind::terrain:state_.destroyed_cells+=e.value;break;
        case EventKind::exit:if(e.value&&state_.score==3)state_.finished=true;break;
        case EventKind::impact:state_.destroyed_cells+=e.value;break;
        }
    }
    acknowledged_tick_=tick;
}
void GameplaySession::present(std::array<std::uint32_t,4>& inventory,std::array<float,4>& timers,
    std::array<std::array<float,4>,3>& targets,std::uint32_t& finished)const{
    inventory={state_.ammunition,state_.grenades,state_.score,state_.shots};
    timers[0]=rifle_ready_>tick_?static_cast<float>(rifle_ready_-tick_)/120.F:0.F;timers[1]=reload_remaining();
    for(unsigned i=0;i<3;++i)targets[i][2]=static_cast<float>(state_.hp[i]);
    finished=state_.finished?1U:0U;
}
}
