// UI values outside the slider ranges (Ctrl+click input, stale settings) must
// reach the world API as valid arguments instead of throwing.
#include "ui/gpu_laboratory.hpp"

#include <iostream>
#include <limits>

int main() {
    using namespace strefa;
    int failures = 0;
    const auto check = [&](bool good, const char* label) {
        if (!good) {
            std::cerr << label << '\n';
            ++failures;
        }
    };
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<float, 4> segment{10, 20, 30, 40};
    for (int tool = -3; tool <= 12; ++tool)
        for (int radius : {-100, 0, 1, 16, 17, 99, std::numeric_limits<int>::max()})
            for (float rate : {-5.F, 0.F, 1e-9F, .5F, 2000.F, 99999.F, 1e9F, inf, -inf, nan}) {
                ui::GpuControls controls;
                controls.thermal_tool = tool;
                controls.radius = radius;
                controls.thermal_rate = rate;
                controls.gas_rate = rate;
                const auto brush = ui::make_thermal_brush(controls, segment);
                check(gpu::valid(brush), "brush from out-of-range UI values is valid");
                check(brush.segment == segment, "brush keeps the stroke segment");
            }
    ui::GpuControls in_range;
    in_range.thermal_tool = static_cast<int>(gpu::ThermalTool::heat);
    in_range.radius = 7;
    in_range.thermal_rate = 350;
    const auto heat = ui::make_thermal_brush(in_range, segment);
    check(heat.radius == 7 && heat.rate == 350 && heat.tool == gpu::ThermalTool::heat, "in-range heat brush unchanged");
    in_range.thermal_tool = static_cast<int>(gpu::ThermalTool::smoke);
    in_range.gas_rate = .25F;
    check(ui::make_thermal_brush(in_range, segment).rate == .25F, "in-range gas brush unchanged");
    check(!gpu::valid({segment, 17, 200, gpu::ThermalTool::heat}) && !gpu::valid({segment, 4, 1e9F, {}})
                  && !gpu::valid({{nan, 0, 0, 0}, 4, 200, {}}),
          "world contract still rejects invalid brushes");
    return failures == 0 ? 0 : 1;
}
