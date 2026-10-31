#pragma once
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace strefa::gpu {
enum class FluidBoundary { unit,volume };
enum class FluidSurface { legacy,akinci2d };
// Immutable world configuration, including diagnostic ablations. Coefficients
// are expressed in the existing cell/second/carrier-mass model, not SI units.
struct FluidOptions {
    FluidBoundary boundary{FluidBoundary::unit};
    FluidSurface surface{FluidSurface::legacy};
    float s_corr{.0001F},tension_scale{1},curvature_scale{1},adhesion{},vorticity{};
    unsigned substeps{2},pbf_iterations{8},contact_iterations{12};
    bool operator==(const FluidOptions&) const=default;
    bool fields()const noexcept{return surface==FluidSurface::akinci2d||vorticity>0;}
    bool velocity_reaction()const noexcept{return adhesion>0;}
    bool valid()const noexcept{
        return (boundary==FluidBoundary::unit||boundary==FluidBoundary::volume)
            &&(surface==FluidSurface::legacy||surface==FluidSurface::akinci2d)
            &&(substeps==2||substeps==4)&&pbf_iterations>0&&pbf_iterations<=16
            &&contact_iterations>=pbf_iterations&&contact_iterations<=24
            &&std::isfinite(s_corr)&&s_corr>=0&&s_corr<=.008F
            &&std::isfinite(tension_scale)&&tension_scale>=0&&tension_scale<=4
            &&std::isfinite(curvature_scale)&&curvature_scale>=0&&curvature_scale<=4
            &&std::isfinite(adhesion)&&adhesion>=0&&adhesion<=20
            &&std::isfinite(vorticity)&&vorticity>=0&&vorticity<=10;
    }
    static FluidOptions balanced(){
        FluidOptions f;f.boundary=FluidBoundary::volume;f.surface=FluidSurface::akinci2d;
        f.adhesion=1;f.vorticity=1;return f;
    }
};
inline std::string fluid_configuration(const FluidOptions& f){
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(std::numeric_limits<float>::max_digits10)
        <<static_cast<int>(f.boundary)<<' '<<static_cast<int>(f.surface)<<' '
        <<f.s_corr<<' '<<f.tension_scale<<' '<<f.curvature_scale<<' '<<f.adhesion<<' '<<f.vorticity<<' '
        <<f.substeps<<' '<<f.pbf_iterations<<' '<<f.contact_iterations;
    return out.str();
}
}
