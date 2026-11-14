#include "gpu/compute_kernels.hpp"

#include "gpu/shader_library.hpp"

#include <initializer_list>
#include <string_view>
#include <vector>

namespace strefa::gpu::detail {
void compile_kernels(ShaderLibrary& library, Api& gl, const ExecutionOptions& execution,
                     const std::string& layout_defines, const std::filesystem::path& shaders, bool debug_groups,
                     const ComputeKernelSet& kernels) {
    struct Submitted {
        ComputeKernel* kernel;
        std::string_view file;
        unsigned op;
    };
    std::vector<Submitted> submitted;
    const auto submit = [&](std::span<ComputeKernel> set, std::string_view file,
                            std::initializer_list<unsigned> operations) {
        for (unsigned op : operations) {
            if (file == "liquids.comp" && op >= 5 && !execution.fluid.fields()) continue;
            const bool separate = execution.specialized || (file == "neighbors.comp" || file == "coupling.comp");
            if (!separate && op != *operations.begin()) continue;
            auto& k = set[separate ? op : 0];
            const bool scalar = (file == "granular.comp" && (op == 10 || op == 12 || op == 20))
                             || (file == "liquids.comp" && op == 3)
                             || (file == "mission.comp" && (op == 0 || op == 1 || op == 4));
            k.local_size = execution.specialized && scalar ? 1U : execution.local_size;
            if (file == "liquids.comp" && !(execution.specialized && scalar))
                k.local_size = execution.effective_pbf_size();
            if (file == "mission.comp" && op == 3 && execution.specialized) k.local_size = execution.reduction_size;
            if (file == "coupling.comp") k.local_size = execution.reduction_size;
            auto defines = "#define LOCAL_SIZE " + std::to_string(k.local_size) + "\n#define PARTICLE_LOCAL_SIZE "
                         + std::to_string(execution.local_size) + "u\n#define GPU_CONDITIONAL_WORK "
                         + (execution.conditional_work ? "1\n" : "0\n");
            defines += layout_defines;
            defines += "#define PBF_LOCAL_SIZE " + std::to_string(execution.effective_pbf_size()) + "u\n";
            const std::string name{file};
            if (separate)
                defines += "#define PASS_OPERATION " + std::to_string(op) + "\n#define PASS_"
                         + name.substr(0, name.find('.')) + "_" + std::to_string(op) + " 1\n";
            if (file == "neighbors.comp") k.local_size = 256;
            k.particle_restrict = execution.effective_pbf_restrict() && file == "liquids.comp" && (op == 1 || op == 4);
            k.id = library.submit({{GL_COMPUTE_SHADER, shaders / name}}, defines, k.particle_restrict);
            k.family = file == "granular.comp" ? 0U : file == "mission.comp" ? 1U : 2U;
            submitted.push_back({&k, file, op});
        }
    };
    submit(kernels.granular, "granular.comp", {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 19, 20});
    submit(kernels.liquids, "liquids.comp", {0, 1, 2, 3, 4, 5, 6});
    if (execution.neighbors != NeighborMode::linked)
        submit(kernels.neighbors, "neighbors.comp", {0, 1, 2, 3, 4, 5, 6, 7});
    submit(kernels.mission, "mission.comp", {0, 1, 3, 4, 5, 6, 7, 8});
    if (execution.coupling != CouplingMode::reference) submit(kernels.coupling, "coupling.comp", {0, 2});
    library.finish();
    for (const auto& [kernel, file, op] : submitted) {
        auto& k = *kernel;
        const auto location = [&](const char* name) { return gl.GetUniformLocation(k.id, name); };
        k.operation = location("operation");
        k.dirty = location("dirtyOnly");
        k.substep = location("substep");
        k.stroke = location("stroke");
        k.radius = location("brushRadius");
        k.material = location("material");
        k.input = location("inputControl");
        k.coupling = location("disableBodyCoupling");
        k.test_count = location("bodyTestCount");
        k.hot_read = location("hotReadBank");
        k.hot_write = location("hotWriteBank");
        k.hot_valid = location("hotValid");
        if (debug_groups) {
            const auto label = std::string{file} + " / " + std::to_string(op) + " / " + std::to_string(k.local_size);
            gl.ObjectLabel(GL_PROGRAM, k.id, -1, label.c_str());
        }
    }
}
} // namespace strefa::gpu::detail
