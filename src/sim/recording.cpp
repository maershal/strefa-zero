#include "sim/recording.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace strefa::sim {
Recording::Recording(const std::filesystem::path& directory) : directory_{directory} {
    std::filesystem::create_directories(directory_);
    log_.open(directory_ / "steps.csv");
    if (!log_) throw std::runtime_error{"Cannot open recording log"};
    log_ << "tick,physics_ms,mass,enthalpy,mass_error,energy_error,escaped_mass,escaped_energy,pcg_residual,pcg_iterations,converged,divergence,overfill\n";
}
void Recording::capture(const World& world, double ms, RenderMode mode) {
    const auto& s = world.statistics();
    log_ << std::setprecision(12) << s.tick << ',' << ms << ',' << s.mass << ',' << s.energy << ','
        << s.mass_error << ',' << s.energy_error << ',' << s.escaped_mass << ',' << s.escaped_energy << ','
        << s.pressure_residual << ',' << s.pressure_iterations << ',' << s.pressure_converged << ','
        << s.max_divergence << ',' << s.max_overfill << '\n';
    if (s.tick % 3 != 0) return;
    world.render(std::span<Color,grid_cells>{pixels_.data(),pixels_.size()},false,mode);
    std::ostringstream name; name << "frame" << std::setw(9) << std::setfill('0') << s.tick << ".bmp";
    std::ofstream file{directory_ / name.str(),std::ios::binary};
    auto bytes = [&](std::uint32_t value,int count) { for(int i=0;i<count;++i) file.put(static_cast<char>((value>>(i*8))&255)); };
    bytes(0x4D42,2); bytes(54+grid_cells*3,4); bytes(0,4); bytes(54,4); bytes(40,4);
    bytes(grid_width,4); bytes(grid_height,4); bytes(1,2); bytes(24,2); bytes(0,4);
    bytes(grid_cells*3,4); bytes(2835,4); bytes(2835,4); bytes(0,4); bytes(0,4);
    for(int y=grid_height-1;y>=0;--y) for(int x=0;x<grid_width;++x) {
        const auto p=pixels_[y*grid_width+x];
        auto over=[&](int value,int background){return static_cast<char>((value*p.a+background*(255-p.a))/255);};
        file.put(over(p.b,25)); file.put(over(p.g,21)); file.put(over(p.r,13));
    }
    if (!file) throw std::runtime_error{"Cannot write recording frame"};
    frames_.push_back(name.str());
}
void Recording::finish() {
    log_.flush();
    std::ofstream html{directory_/"index.html"};
    html << R"(<!doctype html><html lang="pl"><meta charset="utf-8"><title>Strefa Zero 0.4 — zapis</title>
<style>body{margin:3vw;background:#10191f;color:#e1e9d4;font:16px system-ui}img{width:min(100%,960px);image-rendering:pixelated;border:1px solid #748b63;display:block}input{width:min(100%,960px)}button{padding:8px;background:#bdd383}</style>
<h1>Strefa Zero 0.4</h1><p>60 kroków/s · obraz co 3 kroki · odtwarzanie 20 FPS. Czasy w steps.csv obejmują wyłącznie fizykę.</p>
<img id="image" alt="Nagranie sceny"><p><button id="play">Pauza / odtwarzanie</button> <output id="count"></output></p><input id="slider" type="range" min="0" value="0"><script>const frames=[)";
    for(std::size_t i=0;i<frames_.size();++i) { if(i) html<<','; html<<'"'<<frames_[i]<<'"'; }
    html << R"(];let index=0,playing=true;const image=document.getElementById('image'),slider=document.getElementById('slider'),count=document.getElementById('count');slider.max=Math.max(0,frames.length-1);function show(){if(frames.length){image.src=frames[index];slider.value=index;count.textContent=(index+1)+' / '+frames.length;}}document.getElementById('play').onclick=()=>playing=!playing;slider.oninput=()=>{index=+slider.value;playing=false;show();};setInterval(()=>{if(playing&&frames.length){index=(index+1)%frames.length;show();}},50);show();</script></html>)";
    if(!html || !log_) throw std::runtime_error{"Cannot finalize recording"};
}
} // namespace strefa::sim
