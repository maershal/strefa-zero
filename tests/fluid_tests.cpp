#include "app/application.hpp"
#include "app/session_recording.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <chrono>

int main(){
    using namespace strefa;
    try{
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error{text};};
        gpu::FluidOptions legacy,balanced=gpu::FluidOptions::balanced();
        check(legacy.valid()&&balanced.valid()&&legacy!=balanced,"Fluid presets invalid");
        auto invalid=legacy;invalid.s_corr=std::numeric_limits<float>::quiet_NaN();check(!invalid.valid(),"NaN accepted");
        invalid=legacy;invalid.substeps=3;check(!invalid.valid(),"Invalid substep schedule");
        // Independent radial quadrature of the proposed 2D kernels. No GPU or
        // production kernel functions are shared by this oracle.
        const auto cohesion=[](double r){double v=std::pow((2-r)*r,3);return 35840/(209*std::numbers::pi*256)*(r<=1?2*v-1:v);};
        const double alpha=8*std::sqrt(2.0)*std::tgamma(1.75)/(3*std::pow(std::numbers::pi,1.5)*std::tgamma(1.25))*std::pow(2,-2.25);
        double sum=0,adhesion=0;constexpr unsigned n=200000;
        for(unsigned i=0;i<n;++i){const double r=(i+.5)*2/n;sum+=cohesion(r)*2*std::numbers::pi*r*2/n;
            if(r>1)adhesion+=alpha*std::pow(std::max(0.0,-2*r*r+6*r-4),.25)*2*std::numbers::pi*r*2/n;}
        check(std::abs(sum-1)<1e-8,"Cohesion integral is not one");check(std::abs(adhesion-1)<1e-6,"Adhesion normalization");
        check(std::abs(cohesion(1-1e-7)-cohesion(1+1e-7))<1e-7,"Cohesion discontinuity");
        auto parse=[](std::initializer_list<const char*> args){std::vector<char*> argv;for(auto a:args)argv.push_back(const_cast<char*>(a));return app::parse_options(static_cast<int>(argv.size()),argv.data());};
        check(!parse({"game","--gpu-fluid-check"}),"Missing profile accepted");
        check(!parse({"game","--gpu-fluid-vorticity","nan"}),"NaN option accepted");
        check(!parse({"game","--gpu-fluid-s-corr","0.1"}),"Out of range pressure accepted");
        check(!parse({"game","--gpu-fluid-boundary","bad"}),"Unknown boundary accepted");
        check(!parse({"game","--gpu-fluid-substeps","3"}),"Invalid substeps accepted");
        const auto parsed=parse({"game","--gpu-fluid-check","--profile","fluid-tests","--gpu-fluid-profile","balanced","--gpu-fluid-substeps","4"});
        check(parsed&&parsed->fluid_check&&parsed->gpu_execution.fluid.substeps==4&&parsed->gpu_execution.fluid.pbf_iterations==4,"CLI preset/schedule");
        check(!parse({"game","--gpu-render-check"}),"Render check without profile accepted");
        check(!parse({"game","--gpu-render-check","--gpu-render-benchmark","--profile","invalid"}),"Combined render diagnostics accepted");
        check(!parse({"game","--gpu-render-quality","invalid"}),"Unknown render quality accepted");
        const auto render=parse({"game","--gpu-render-quality","high"});
        check(render&&render->gpu_render.quality==gpu::RenderQuality::high&&render->gpu_execution.fluid.substeps==2&&render->gpu_execution.fluid.s_corr==.0001F,"Render quality changed physical preset");
        const auto folder=std::filesystem::temp_directory_path()/("strefa-fluid-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder);
        app::SessionRecording recording;recording.begin();recording.append(app::SceneConfiguration{});recording.append(gpu::MissionInput{1,2,3,0});
        gpu::ExecutionOptions execution;execution.fluid=balanced;gpu::ProfileOptions profile;
        const auto file=folder/"session.txt";check(bool(recording.save(file,execution,profile)),"v2 save");
        const auto loaded=app::SessionRecording::load(file,execution,profile);check(loaded&&loaded->size()==2,"v2 roundtrip");
        auto mismatch=execution;mismatch.fluid.s_corr=.0005F;check(!app::SessionRecording::load(file,mismatch,profile),"Mismatched fluid options accepted");
        std::ifstream in{file};std::string header,fluid,config,body,line;std::getline(in,header);std::getline(in,fluid);std::getline(in,config);
        while(std::getline(in,line)){body+=line+'\n';}
        in.close();
        {std::ofstream out{file};out<<"STREFA_SESSION 1\n"<<config<<'\n'<<body;}
        check(!app::SessionRecording::load(file,execution,profile),"v1 accepted with new physics");
        execution.fluid={};check(bool(app::SessionRecording::load(file,execution,profile)),"v1 legacy compatibility");
        {std::ofstream out{file};out<<"STREFA_SESSION 2\nI07.999 0\n"<<config<<'\n'<<body;}
        check(!app::SessionRecording::load(file,execution,profile),"Unknown revision accepted");
        app::SessionRecording thermal;gpu::ThermalOptions t;t.enabled=true;thermal.begin(t);thermal.append(app::SceneConfiguration{false,gpu::Scene::empty,2});thermal.append(gpu::ThermalBrush{{20,20,30,30},4,120,gpu::ThermalTool::heat});thermal.append(gpu::MissionInput{});thermal.append(gpu::ThermalOptions{});
        check(bool(thermal.save(file,execution,profile)),"v3 thermal save");const auto v3=app::SessionRecording::load(file,execution,profile);check(v3&&v3->size()==5&&std::holds_alternative<gpu::ThermalOptions>((*v3)[0])&&std::get<app::SceneConfiguration>((*v3)[1]).thermal_scene==2,"v3 thermal roundtrip");
        {std::ofstream out{file,std::ios::app};out<<"E 1 1 1 1 4 nan 1\n";}check(!app::SessionRecording::load(file,execution,profile),"nonfinite thermal dose rejected");
        check(!parse({"game","--gpu-thermal","invalid"}),"invalid thermal mode");check(!parse({"game","--gpu-thermal-check"}),"thermal check requires profile");check(!parse({"game","--gpu-thermal-check","--gpu-vfx-check","--profile","invalid"}),"exclusive thermal checks");check(bool(parse({"game","--gpu-thermal-scene","quench"})),"thermal scene CLI");
        std::filesystem::remove_all(folder);
        std::cout<<"PASS: independent 2D quadrature, presets, invalid coefficients, CLI, recording v2 and v1 compatibility\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
