#include "game/gameplay_session.hpp"
#include "sim/fixed_clock.hpp"
#include "game/session.hpp"
#include <deque>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace strefa;
void check(bool v,const char* message){if(!v)throw std::runtime_error{message};}
game::EventBatch packet(const game::CommandBatch& c,bool accept=true){
    game::EventBatch b;b.header={0,c.header[1],c.header[0],0};
    for(unsigned i=0;i<c.header[2];++i){const auto& cmd=c.commands[i];auto& e=b.events[b.header[0]++];
        e={cmd.generation,cmd.tick,i+1,accept?1U:2U,cmd.id,0,0,0,{},{}};
    }return b;
}
void add(game::EventBatch& b,game::EventKind kind,unsigned subject,unsigned value){
    auto& e=b.events[b.header[0]++];e={b.header[2],b.header[1],b.header[0],static_cast<unsigned>(kind),0,subject,value,0,{},{}};
}
int main(){try{
    game::GameplaySession s;s.reset(9);auto c=s.prepare({0,0,0,20});check(c.header[2]==2&&s.state().ammunition==29&&s.state().grenades==2,"Resource reservation");
    auto b=packet(c,false);s.consume(b);s.consume(b);check(s.state().ammunition==30&&s.state().grenades==3&&s.pending()==0,"Refund and duplicate ack");
    c=s.prepare({0,0,0,20});check(c.header[2]==0,"Both cooldowns use ticks");s.consume(packet(c));
    for(int i=0;i<11;++i){c=s.prepare({});s.consume(packet(c));}
    c=s.prepare({0,0,0,4});check(c.header[2]==1,"Rifle interval preserves legacy 13 ticks");s.consume(packet(c));
    c=s.prepare({0,0,0,8});check(s.reload_remaining()>1.508F&&s.reload_remaining()<1.509F,"Reload starts on tick");s.consume(packet(c));
    for(int i=0;i<181;++i){c=s.prepare({0,0,0,4});s.consume(packet(c));}check(s.state().ammunition==29&&s.state().shots==2,"Reload completes then fires once");
    c=s.prepare({});b=packet(c);for(unsigned j=0;j<3;++j)add(b,game::EventKind::hit,j,3);add(b,game::EventKind::exit,0,1);s.consume(b);s.consume(b);
    check(s.state().score==3&&s.state().finished,"One CPU owner of goals and score");
    s.reset(10);s.consume(b);check(s.state().score==0&&s.pending()==0,"Reset rejects old generation");
    c=s.prepare({});b=packet(c);b.header[1]++;bool rejected=false;try{s.consume(b);}catch(const std::runtime_error&){rejected=true;}check(rejected,"Missing batch rejected");
    b=packet(c);b.header[3]=1;rejected=false;try{s.consume(b);}catch(const std::runtime_error&){rejected=true;}check(rejected,"Overflow rejected");
    s.reset(11,{},std::numeric_limits<unsigned>::max()-256);rejected=false;try{s.prepare({});}catch(const std::overflow_error&){rejected=true;}check(rejected,"No silent counter wrap");
    game::CommandRing ring;game::GameplayState state;
    for(unsigned i=1;i<=game::command_capacity;++i){game::WorldCommand cmd;cmd.id=i;check(ring.reserve(cmd),"Journal capacity");}
    game::WorldCommand cmd;cmd.id=999;check(!ring.reserve(cmd),"Journal overflow admission");check(ring.acknowledge(2,true,state)&&ring.reserve(cmd),"Journal slot reused only after ack");
    game::GameplayState reference;
    for(int fps:{30,60,144}){
        game::GameplaySession session;session.reset(1);sim::FixedStepClock<120,4,2147483647> clock;
        std::deque<std::pair<unsigned,game::EventBatch>> pending;unsigned frame=0;
        while(session.tick()<1200){
            // Three render frames of latency, then consume in bursts.
            while(!pending.empty()&&pending.front().first<=frame){session.consume(pending.front().second);session.consume(pending.front().second);pending.pop_front();}
            auto steps=clock.advance(1.0/fps,false,1);
            for(int j=0;j<steps&&session.tick()<1200;++j){
                const auto t=session.tick();auto commands=session.prepare({0,0,0,4U|((t==45||t==50||t==90)?16U:0U)|(t==500?8U:0U)});
                auto events=packet(commands);if(t==1100){for(unsigned target=0;target<3;++target)add(events,game::EventKind::hit,target,3);}
                if(t==1150)add(events,game::EventKind::exit,0,1);
                pending.emplace_back(frame+3,events);
            }++frame;
        }
        for(auto& item:pending)session.consume(item.second);
        const auto result=session.state();check(result.finished&&result.score==3&&session.pending()==0,"Delayed critical events complete");
        if(fps==30)reference=result;else check(result.ammunition==reference.ammunition&&result.grenades==reference.grenades&&result.shots==reference.shots,"30/60/144 rules invariant");
    }
    game::InputBuffer input;game::TickInput press;press.fire=press.jump=press.grenade=true;input.submit(press);input.submit({});const auto consumed=input.consume();
    check(consumed.fire&&consumed.jump&&consumed.grenade&&!input.consume().fire,"Zero-tick render preserves taps");
    sim::FixedStepClock<120,4,2147483647> debt;check(debt.advance(1,false,1)==4,"Debt clock");debt.defer(4);unsigned total=0;while(auto n=debt.advance(0,false,1))total+=n;check(total==120&&debt.dropped_seconds()==0,"Backpressure keeps elapsed time");
    std::cout<<"PASS: reservations, refunds, dedup, cooldown/reload, reset, overflow, wrap, 3-frame latency, 30/60/144 FPS, input and clock debt\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
