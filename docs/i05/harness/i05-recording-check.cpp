#include "app/session_recording.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace strefa;
int main(){
    const auto folder=std::filesystem::path{"work/i05-results/recording-guards"};std::filesystem::create_directories(folder);
    app::SessionRecording recording;recording.begin();recording.append(app::SceneConfiguration{true});
    recording.append(gpu::MissionInput{1,55,140,gpu::jump|gpu::grenade});
    recording.append(gpu::Stroke{8,8,12,12,3,gpu::Material::water});
    recording.append(gpu::MissionInput{0,60,150,0});
    auto file=folder/"roundtrip.txt";
    if(!recording.save(file,{},{}))throw std::runtime_error{"save failed"};
    const auto loaded=app::SessionRecording::load(file,{},{});
    if(!loaded||loaded->size()!=4||!std::get<app::SceneConfiguration>((*loaded)[0]).mission
        ||std::get<gpu::MissionInput>((*loaded)[1]).buttons!=(gpu::jump|gpu::grenade)
        ||std::get<gpu::Stroke>((*loaded)[2]).material!=gpu::Material::water)throw std::runtime_error{"roundtrip failed"};
    std::ifstream source{file};std::string header,config;std::getline(source,header);std::getline(source,config);
    unsigned rejected{};
    const auto invalid=[&](const std::string& name,const std::string& data){
        const auto path=folder/(name+".txt");std::ofstream{path}<<data;
        if(app::SessionRecording::load(path,{},{}))throw std::runtime_error{"accepted "+name};
        ++rejected;
    };
    const auto prefix=header+'\n'+config+"\nR 1 9\n";
    invalid("schema","STREFA_SESSION 99\n");
    invalid("configuration",header+"\nwrong\nR 1 9\n");
    invalid("missing_reset",header+'\n'+config+"\nT 0 1 1 0\n");
    invalid("unknown_command",prefix+"X\n");
    invalid("trailing_payload",prefix+"T 0 1 1 0 extra\n");
    invalid("nonfinite",prefix+"T nan 1 1 0\n");
    invalid("invalid_buttons",prefix+"T 0 1 1 32\n");
    invalid("invalid_scene",prefix+"R 0 12\n");
    invalid("invalid_material",prefix+"P 1 1 2 2 4 7\n");
    invalid("invalid_radius",prefix+"P 1 1 2 2 17 1\n");
    invalid("long_row",prefix+std::string(513,'T')+'\n');
    for(std::size_t i=0;i<app::SessionRecording::capacity;++i)recording.append(gpu::MissionInput{});
    if(recording.save(folder/"overflow.txt",{},{})||!recording.overflowed())throw std::runtime_error{"overflow accepted"};
    ++rejected;
    std::cout<<"roundtrip=PASS\nnegative_cases="<<rejected<<"\npassed=1\n";
}
