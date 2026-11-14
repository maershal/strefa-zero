#include "app/diagnostic_scenarios.hpp"
#include "gpu/shader_library.hpp"

#include <array>
#include <chrono>
#include <fstream>

namespace strefa::app {
std::expected<int, std::string> validate_shader_cache(const std::filesystem::path& output) {
    try {
        const auto root = output / "cache-fixture";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "sources");
        const auto shader = root / "sources" / "test.comp";
        std::ofstream{shader}
                << "#version 430\n#include \"particle_common.glsl\"\nlayout(local_size_x=1) in;\nlayout(std430,binding=0) buffer Result {uint value;};\nvoid main(){value=VALUE+EXTRA;}\n";
        auto include = [&](unsigned value) {
            std::ofstream{root / "sources" / "particle_common.glsl"} << "#define VALUE " << value << "u\n";
        };
        include(7);
        gpu::detail::Api api;
        api.load();
        GLuint buffer{};
        api.GenBuffers(1, &buffer);
        api.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        api.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(unsigned), nullptr, GL_DYNAMIC_COPY);
        struct Cleanup {
            gpu::detail::Api& gl;
            GLuint id;
            ~Cleanup() { gl.DeleteBuffers(1, &id); }
        } cleanup{api, buffer};
        unsigned cases{};
        auto run = [&](const std::string& defines, unsigned expected, bool hit, bool reject) {
            gpu::detail::ShaderLibrary library{api};
            library.configure_cache(root / "binaries");
            const auto program = library.program({{GL_COMPUTE_SHADER, shader}}, defines);
            api.UseProgram(program);
            api.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
            api.DispatchCompute(1, 1, 1);
            api.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            unsigned value{};
            api.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            api.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(value), &value);
            const auto stats = library.stats();
            if (value != expected || stats.hits != (hit ? 1U : 0U) || stats.rejected != (reject ? 1U : 0U)
                || glGetError() != GL_NO_ERROR)
                throw std::runtime_error{"Cache shaderów: niepoprawny wynik, licznik lub błąd GL."};
            ++cases;
            return stats.supported;
        };
        // Batched submission (parallel where the driver allows it) and the forced sequential path give the
        // same results; a broken shader names its file, stage and defines and leaves the library usable.
        bool parallel{};
        const bool advertised = SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile")
                             || SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile");
        for (const bool allowed : {true, false}) {
            gpu::detail::ShaderLibrary library{api, allowed ? gpu::detail::ParallelCompile::automatic
                                                            : gpu::detail::ParallelCompile::off};
            unsigned reports{};
            library.set_progress([&](unsigned done, unsigned total) { reports += done <= total ? 1U : 1000U; });
            std::array<GLuint, 3> programs{};
            for (unsigned i = 0; i < programs.size(); ++i)
                programs[i] =
                        library.submit({{GL_COMPUTE_SHADER, shader}}, "#define EXTRA " + std::to_string(i) + "u\n");
            library.finish();
            for (unsigned i = 0; i < programs.size(); ++i) {
                api.UseProgram(programs[i]);
                api.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
                api.DispatchCompute(1, 1, 1);
                api.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                unsigned value{};
                api.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
                api.GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(value), &value);
                if (value != 7 + i) throw std::runtime_error{"Kompilacja wsadowa: niepoprawny wynik programu."};
            }
            if (allowed) parallel = library.parallel();
            // A driver that advertises the extension must take the parallel path, so CI exercises it.
            if (library.stats().programs != programs.size() || reports < programs.size() || reports >= 1000
                || (!allowed && library.parallel()) || (allowed && advertised != library.parallel()))
                throw std::runtime_error{"Kompilacja wsadowa: niepoprawne liczniki postępu lub programów."};
            const auto broken = root / "sources" / "broken.comp";
            std::ofstream{broken} << "#version 430\nlayout(local_size_x=1) in;\nvoid main(){ missing_symbol(); }\n";
            std::string message;
            try {
                library.submit({{GL_COMPUTE_SHADER, shader}}, "#define EXTRA 0u\n");
                library.submit({{GL_COMPUTE_SHADER, broken}}, "#define BROKEN_MARK 1\n");
                library.finish();
            } catch (const std::runtime_error& error) {
                message = error.what();
            }
            for (const auto* part : {"broken.comp", "etap obliczeniowy", "#define BROKEN_MARK 1", "Log sterownika"})
                if (message.find(part) == std::string::npos)
                    throw std::runtime_error{"Błąd kompilacji bez pliku, etapu lub definicji: " + message};
            // A failed batch is deleted whole; one by one, the program before the broken one was its own batch.
            if (library.size() != programs.size() + (library.parallel() ? 0U : 1U)
                || library.program({{GL_COMPUTE_SHADER, shader}}, "#define EXTRA 0u\n") == 0
                || glGetError() != GL_NO_ERROR)
                throw std::runtime_error{"Po błędzie kompilacji biblioteka zostawiła programy partii lub błąd GL."};
            // Cancelling from the progress callback (the window closed) deletes the batch, counts nothing and
            // writes no program binary for it.
            {
                gpu::detail::ShaderLibrary cancelled{api, allowed ? gpu::detail::ParallelCompile::automatic
                                                                  : gpu::detail::ParallelCompile::off};
                const auto binaries = root / ("cancelled-" + std::to_string(allowed));
                cancelled.configure_cache(binaries);
                cancelled.set_progress([](unsigned done, unsigned) {
                    if (done >= 1) throw std::runtime_error{"cancelled"};
                });
                bool thrown{};
                try {
                    for (unsigned i = 0; i < 3; ++i)
                        cancelled.submit({{GL_COMPUTE_SHADER, shader}},
                                         "#define EXTRA " + std::to_string(i + 4) + "u\n");
                    cancelled.finish();
                } catch (const std::runtime_error& error) {
                    thrown = std::string_view{error.what()} == "cancelled";
                }
                const auto stats = cancelled.stats();
                if (!thrown || cancelled.size() != 0 || stats.programs != 0 || stats.source_builds != 0
                    || (std::filesystem::exists(binaries) && !std::filesystem::is_empty(binaries))
                    || glGetError() != GL_NO_ERROR)
                    throw std::runtime_error{"Przerwana kompilacja zostawiła programy, liczniki lub binarki partii."};
            }
            ++cases;
        }
        const std::string defines = "#define EXTRA 0u\n";
        // Without program binaries only the source fallback ran: report a CTest skip, not a pass.
        if (!run(defines, 7, false, false)) {
            std::ofstream{output / "shader-cache-check.txt"}
                    << "supported=0\nsource_fallback=1\nskipped=1\nparallel_compile=" << parallel << '\n';
            return 77;
        }
        run(defines, 7, true, false);
        for (const auto& entry : std::filesystem::directory_iterator(root / "binaries"))
            std::ofstream{entry.path(), std::ios::binary | std::ios::trunc} << "corrupt";
        run(defines, 7, false, true);
        run(defines, 7, true, false);
        include(11);
        run(defines, 11, false, false);
        run(defines, 11, true, false);
        run("#define EXTRA 2u\n", 13, false, false);
        run("#define EXTRA 2u\n", 13, true, false);
        api.UseProgram(0);
        std::ofstream report{output / "shader-cache-check.txt"};
        report << "supported=1\nparallel_compile=" << parallel << "\ncases=" << cases << "\npassed=1\n";
        return 0;
    } catch (const std::exception& error) {
        return std::unexpected{error.what()};
    }
}
} // namespace strefa::app
