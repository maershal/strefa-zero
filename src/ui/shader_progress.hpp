#pragma once

namespace strefa::ui {
// One loading frame while GPU programs build: "Kompilacja shaderów… n/m" and a progress bar.
// Call between Session::new_frame() and Session::render().
void draw_shader_progress(unsigned done, unsigned total, float scale);
} // namespace strefa::ui
