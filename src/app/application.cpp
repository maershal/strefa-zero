#include "app/application.hpp"
#include "platform/settings_store.hpp"
#include "platform/texture.hpp"
#include "platform/window.hpp"
#include "ui/session.hpp"
#include "ui/view.hpp"
#include "ui/laboratory.hpp"
#include "ui/arena.hpp"
#include "sim/fixed_clock.hpp"
#include "sim/recording.hpp"
#include "build_info.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace strefa::app {
namespace {

std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path{reinterpret_cast<const char8_t*>(text)};
}
std::string gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unknown";
}

class Application {
public:
    Application(platform::Window& window, ui::Session& gui, const platform::SettingsStore& store,
        core::Settings settings, std::array<ui::Image, 3> images, platform::Texture& canvas,
        ui::Image soldier, ui::Image parts, ui::Image weapons, std::string message, sim::PhysicsConfig physics)
        : window_{window}, gui_{gui}, store_{store}, images_{images}, canvas_{canvas}, soldier_{soldier},
          parts_{parts}, weapons_{weapons}, state_{ui::Screen::main_menu, settings, settings, settings.show_diagnostics},
          message_{std::move(message)}, renderer_{gl_string(GL_RENDERER)}, gl_version_{gl_string(GL_VERSION)},
          physics_{physics},game_{{},{},physics} {

    }

    std::expected<int, std::string> run(bool smoke_test, bool start_in_laboratory, bool start_in_arena, bool lab_smoke) {
        if(smoke_test) {
            world_=std::make_unique<sim::World>(0x57EF'A023U,physics_);world_->load_demo();
            lab_snapshot_=sim::present_world(*world_,sim::RenderMode::materials);
        } else {
            lab_worker_=std::make_unique<sim::LaboratoryRunner>(store_.path().parent_path()/"recordings",
                lab_smoke?sim::Scene::dam_break:sim::Scene::demonstration,physics_);
            lab_snapshot_=lab_worker_->snapshot();
        }
        if (start_in_laboratory && !smoke_test) perform(ui::Action::open_laboratory);
        if (start_in_arena && !smoke_test) perform(ui::Action::open_arena);
        if(lab_smoke) {lab_controls_.scene=sim::Scene::dam_break;perform(ui::Action::open_laboratory);}
        std::array<bool, 6> visited_screens{};
        std::array<bool, 3> visited_themes{};
        bool smoke_ok = true;
        unsigned frames{};
        std::vector<double> ui_frame_times;
        auto previous = SDL_GetTicksNS();
        while (running_) {
            const auto frame_start = SDL_GetTicksNS();
            const float frame_ms = static_cast<float>(frame_start - previous) / 1'000'000.0F;
            previous = frame_start;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                gui_.process_event(event);
                if (event.type == SDL_EVENT_QUIT
                    || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                        && event.window.windowID == SDL_GetWindowID(window_.native()))) running_ = false;
            }
            if (!running_) break;
            if (window_.minimized()) { if(lab_worker_) lab_worker_->set_active(false); clock_.reset(); game_clock_.reset(); game_input_.clear(); SDL_Delay(30); continue; }

            if (smoke_test) smoke_ok = smoke_step(frames) && smoke_ok;
            if(lab_smoke) {
                if(frames>=30) ui_frame_times.push_back(frame_ms);
                if(frames==120) lab_controls_.paused=true;
                if(frames==150) lab_controls_.paused=false;
                if(frames==200) perform(ui::Action::back);
                if(frames==220) perform(ui::Action::open_laboratory);
            }
            gui_.set_scale(window_.display_scale(), state_.applied.ui_scale);
            gui_.new_frame();
            visited_screens[static_cast<std::size_t>(state_.screen)] = true;
            visited_themes[static_cast<std::size_t>(state_.applied.map_theme)] = true;
            const ui::FrameInfo info{gui_.scale(), frame_ms, renderer_, gl_version_, message_};
            // Render one coherent snapshot of actors and terrain. Input collected by
            // the UI advances the next snapshot; no GPU view can mutate the world.
            if (state_.screen == ui::Screen::arena) {
                const auto uploaded = upload_world(game_.world(), true);
                if (!uploaded) return std::unexpected{uploaded.error()};
                ++game_uploads_;
            }
            if(state_.screen==ui::Screen::laboratory) {
                if(lab_worker_) lab_snapshot_=lab_worker_->snapshot();
                if(!lab_snapshot_->error.empty()) return std::unexpected{lab_snapshot_->error};
                if(last_lab_pixels_!=lab_snapshot_->pixels) {
                    const auto uploaded=canvas_.update_rgba(std::as_bytes(std::span{*lab_snapshot_->pixels}));
                    if(!uploaded) return std::unexpected{uploaded.error()};
                    last_lab_pixels_=lab_snapshot_->pixels;++uploads_;
                }
                lab_controls_.recording=lab_snapshot_->recording;
            }
            ui::LaboratoryInput input;
            ui::LaboratoryFrame lab{{canvas_.id(), canvas_.width(), canvas_.height()}, *lab_snapshot_,
                lab_controls_, input, lab_snapshot_->step_ms, lab_snapshot_->overload_seconds};
            ui::ArenaInput arena_input;
            const bool focused = (SDL_GetWindowFlags(window_.native()) & SDL_WINDOW_INPUT_FOCUS) != 0;
            ui::ArenaFrame arena{{canvas_.id(), canvas_.width(), canvas_.height()}, soldier_,
                images_[static_cast<std::size_t>(state_.applied.map_theme)], parts_, weapons_, game_, arena_input, focused};
            const auto action = ui::draw(state_, info, images_, gui_.fonts(), &lab, &arena);
            if (state_.screen == ui::Screen::laboratory) {
                const auto updated = update_lab(input, static_cast<double>(frame_ms) / 1000.0);
                if (!updated) return std::unexpected{updated.error()};
            } else { if(lab_worker_) lab_worker_->set_active(false); clock_.reset(); lab_controls_.previous.reset(); }
            if (state_.screen == ui::Screen::arena && action != ui::Action::back)
                update_game(arena_input, static_cast<double>(frame_ms) / 1000.0, focused, smoke_test);
            else { game_clock_.reset(); game_input_.clear(); }
            window_.clear();
            gui_.render();
            if (smoke_test && glGetError() != GL_NO_ERROR)
                return std::unexpected{"Test renderowania wykrył błąd OpenGL."};
            window_.present();
            if (action != ui::Action::none) perform(action);
            ++frames;
            if (smoke_test && frames >= 238) running_ = false;
            if(lab_smoke&&frames>=300) running_=false;

            if (!window_.vsync_enabled()) {
                const auto budget = static_cast<Uint64>(1'000'000'000 / state_.applied.frame_limit);
                const auto elapsed = SDL_GetTicksNS() - frame_start;
                if (elapsed < budget) SDL_DelayPrecise(budget - elapsed);
            }
        }
        if(lab_worker_) {
            lab_worker_->stop();
            if(!lab_worker_->snapshot()->error.empty()) return std::unexpected{lab_worker_->snapshot()->error};
        }
        if(lab_smoke) {
            const auto snapshot=lab_worker_->snapshot();
            std::ranges::sort(ui_frame_times);
            auto percentile=[&](double q){return ui_frame_times[std::min(ui_frame_times.size()-1,static_cast<std::size_t>(ui_frame_times.size()*q))];};
            std::ofstream report{store_.path().parent_path()/"laboratory-performance.txt"};
            if(!report) return std::unexpected{"Nie można zapisać raportu laboratorium."};
            report<<"frames="<<frames<<"\nui_median_ms="<<percentile(.5)<<"\nui_p95_ms="<<percentile(.95)
                <<"\nui_p99_ms="<<percentile(.99)<<"\nui_max_ms="<<ui_frame_times.back()
                <<"\nphysics_last_ms="<<snapshot->step_ms<<"\nphysics_ticks="<<snapshot->report.tick
                <<"\nphysics_steps_per_second="<<snapshot->steps_per_second<<"\nphysics_threads="<<snapshot->report.worker_threads
                <<"\nfinite="<<snapshot->report.finite<<'\n';
            if(!snapshot->ready||!snapshot->report.finite||snapshot->report.tick<5||percentile(.95)>100
                ||(physics_.worker_threads&&snapshot->report.worker_threads!=physics_.worker_threads))
                return std::unexpected{"Test reaktywności laboratorium nie przeszedł."};
        }
        if (recording_) { recording_->finish(); recording_.reset(); }
        if (smoke_test) {
            const bool all_screens = std::ranges::all_of(visited_screens, [](bool value) { return value; });
            const bool all_themes = std::ranges::all_of(visited_themes, [](bool value) { return value; });
            const bool all_images = std::ranges::all_of(images_, [](const auto& image) { return image.handle != 0; });
            std::ofstream report{store_.path().parent_path() / "smoke-report.txt"};
            report << "frames=" << frames << "\nall_screens=" << all_screens
                << "\nall_themes=" << all_themes << "\nall_backgrounds=" << all_images
                << "\nsettings_applied=" << smoke_ok << "\nrenderer=" << renderer_
                << "\nopengl=" << gl_version_ << "\ncompiler=" << build::compiler
                << "\ncpp=" << __cplusplus << "\nsimulation_uploads=" << uploads_
                << "\nsimulation_pause_step=" << smoke_lab_ok_ << "\nactor_atlas=" << (soldier_.handle != 0)
                << "\ngame_uploads=" << game_uploads_ << "\ngame_ticks=" << game_.ticks()
                << "\nshots_fired=" << game_.shots_fired() << "\ngrenade_explosions=" << game_.explosions() << '\n';
            report<<"layered_parts="<<(parts_.handle!=0)<<"\nweapon_atlas="<<(weapons_.handle!=0)
                <<"\nphysics_finite="<<world_->statistics().finite<<"\ngame_physics_finite="<<game_.world().statistics().finite<<'\n';
            if (!all_screens || !all_themes || !all_images || !smoke_ok || uploads_ == 0 || !smoke_lab_ok_
                || !soldier_.handle || !parts_.handle || !weapons_.handle || !world_->statistics().finite || !game_.world().statistics().finite
                || game_uploads_ == 0 || game_.shots_fired() == 0 || game_.explosions() != 1)
                return std::unexpected{"Niepełny wynik testu aplikacji. Sprawdź smoke-report.txt."};
        }
        return 0;
    }

private:
    std::expected<void, std::string> upload_world(const sim::World& world, bool transparent = false) {
        last_lab_pixels_.reset();
        world.render(std::span<sim::Color, sim::World::cell_count>{pixels_.data(), pixels_.size()}, transparent,
            transparent ? sim::RenderMode::materials : lab_controls_.view);
        return canvas_.update_rgba(std::as_bytes(std::span{pixels_}));
    }
    void update_game(ui::ArenaInput input, double elapsed, bool focused, bool smoke) {
        if (input.reset) { game_.reset(); game_clock_.reset(); game_input_.clear(); elapsed = 0; }
        if (!focused && !smoke) { game_clock_.reset(); game_input_.clear(); return; }
        if (smoke) {
            input.controls = {};
            input.controls.aim = {184, 140};
            input.controls.move = smoke_game_frame_ < 40 ? 1.F : 0.F;
            input.controls.jump = smoke_game_frame_ == 3;
            input.controls.jet = smoke_game_frame_ > 10 && smoke_game_frame_ < 90;
            input.controls.fire = true;
            input.controls.grenade = smoke_game_frame_ == 2;
            input.controls.reload = smoke_game_frame_ == 120;
            ++smoke_game_frame_; elapsed = sim::FixedClock::tick_seconds;
        }
        game_input_.submit(input.controls);
        const int steps = game_clock_.advance(elapsed, false, 1);
        for (int i = 0; i < steps; ++i) game_.tick(game_input_.consume());
    }
    std::expected<void, std::string> update_lab(const ui::LaboratoryInput& input, double elapsed) {
        if(lab_worker_) {
            sim::LaboratoryControl control{true,lab_controls_.paused,lab_controls_.speed,lab_controls_.view,input.probe};
            sim::LaboratoryCommand command;
            if(input.reset) command.scene=lab_controls_.scene;
            command.clear=input.clear;command.toggle_boundary=input.boundary_toggle;command.toggle_recording=input.record_toggle;
            command.manual_steps=input.single_step?1:0;command.stroke=input.stroke;
            if(input.heat) command.heat=sim::HeatSource{*input.heat,lab_controls_.tool==1?100.0:-100.0};
            const bool event=!command.empty();
            const auto ticket=lab_worker_->submit(control,std::move(command));
            if(event&&ticket==0) message_="Kolejka edycji jest pełna. Zatrzymaj pędzel na chwilę.";
            return {};
        }
        if (input.reset || input.clear) {
            if (recording_) { recording_->finish(); recording_.reset(); lab_controls_.recording=false; }
            if (input.reset) world_->load_scene(lab_controls_.scene); else world_->clear();
            clock_.reset(); lab_controls_.previous.reset(); elapsed = 0;
        }
        if (input.boundary_toggle) world_->set_open_top(!world_->config().open_top);
        if (input.heat) world_->inject_heat(*input.heat, lab_controls_.tool == 1 ? 100 : -100);
        if (input.record_toggle) {
            try {
                if (recording_) { recording_->finish(); recording_.reset(); }
                else recording_.emplace(store_.path().parent_path()/"recordings"/std::to_string(SDL_GetTicksNS()));
                lab_controls_.recording=recording_.has_value();
            } catch(const std::exception& e) { return std::unexpected{e.what()}; }
        }
        if (input.stroke) {
            const auto& stroke = *input.stroke;
            world_->paint(stroke.from, stroke.to, stroke.radius, stroke.material, stroke.erase);
        }
        const int steps = clock_.advance(elapsed, lab_controls_.paused, lab_controls_.speed,
            input.single_step || std::exchange(smoke_single_step_, false));
        for (int i = 0; i < steps; ++i) {
            const auto start = SDL_GetPerformanceCounter(); world_->step();
            step_ms_ = static_cast<double>(SDL_GetPerformanceCounter()-start)*1000.0/static_cast<double>(SDL_GetPerformanceFrequency());
            if (recording_) {
                try { recording_->capture(*world_,step_ms_,lab_controls_.view); }
                catch(const std::exception& e) { return std::unexpected{e.what()}; }
            }
        }
        auto snapshot=sim::present_world(*world_,lab_controls_.view,input.probe);
        snapshot->step_ms=step_ms_;snapshot->recording=recording_.has_value();lab_snapshot_=std::move(snapshot);
        const auto uploaded = upload_world(*world_);
        if (uploaded) ++uploads_;
        return uploaded;
    }

    bool perform(ui::Action action) {
        switch (action) {
            case ui::Action::none: break;
            case ui::Action::open_arena:
                state_.screen = ui::Screen::arena; game_clock_.reset(); game_input_.clear(); message_.clear(); break;
            case ui::Action::open_laboratory:
                state_.screen = ui::Screen::laboratory; clock_.reset(); message_.clear(); break;
            case ui::Action::open_preview: state_.screen = ui::Screen::preview; message_.clear(); break;
            case ui::Action::open_settings:
                state_.draft = state_.applied; state_.screen = ui::Screen::settings; message_.clear(); break;
            case ui::Action::open_about: state_.screen = ui::Screen::about; message_.clear(); break;
            case ui::Action::back:
                state_.draft = state_.applied; state_.screen = ui::Screen::main_menu; message_.clear(); break;
            case ui::Action::reset: state_.draft = core::Settings{}; message_.clear(); break;
            case ui::Action::quit: running_ = false; break;
            case ui::Action::apply: {
                if (const auto checked = core::validate(state_.draft); !checked) {
                    message_ = checked.error(); return false;
                }
                if (const auto changed = window_.apply_video(state_.draft, state_.applied); !changed) {
                    message_ = changed.error(); return false;
                }
                state_.applied = state_.draft;
                state_.diagnostics = state_.applied.show_diagnostics;
                if (const auto saved = store_.save(state_.applied); !saved) {
                    message_ = saved.error() + " Zmiany działają w bieżącej sesji."; return false;
                }
                message_ = "Ustawienia zostały zapisane.";
                break;
            }
        }
        return true;
    }

    bool smoke_step(unsigned frame) {
        switch (frame) {
            case 16: return perform(ui::Action::open_settings);
            case 24:
                state_.draft.map_theme = core::MapTheme::industrial;
                state_.draft.width = 1100; state_.draft.height = 720;
                state_.draft.vsync = false; state_.draft.ui_scale = 1.15F;
                return perform(ui::Action::apply);
            case 32: return perform(ui::Action::open_preview);
            case 40: return perform(ui::Action::open_settings);
            case 41: state_.draft.map_theme = core::MapTheme::winter; return perform(ui::Action::apply);
            case 48: return perform(ui::Action::open_about);
            case 56: return perform(ui::Action::open_laboratory);
            case 57:
                world_->paint({105, 100}, {109, 115}, 3, sim::Material::fire);
                world_->paint({240, 120}, {255, 120}, 4, sim::Material::lava);
                break;
            case 58: lab_controls_.paused = true; smoke_paused_tick_ = world_->statistics().tick; break;
            case 59: smoke_lab_ok_ = world_->statistics().tick == smoke_paused_tick_; break;
            case 60: smoke_single_step_ = true; break;
            case 61: smoke_lab_ok_ = smoke_lab_ok_ && world_->statistics().tick == smoke_paused_tick_ + 1; break;
            case 62: lab_controls_.paused = false; break;
            case 78: return perform(ui::Action::back);
            case 80: return perform(ui::Action::open_arena);
            case 230: return perform(ui::Action::back);
            default: return true;
        }
        return true;
    }

    platform::Window& window_;
    ui::Session& gui_;
    const platform::SettingsStore& store_;
    std::array<ui::Image, 3> images_;
    platform::Texture& canvas_;
    ui::Image soldier_;
    ui::Image parts_, weapons_;
    ui::ViewState state_;
    bool running_{true};
    std::string message_;
    std::string renderer_;
    std::string gl_version_;
    std::unique_ptr<sim::World> world_;
    std::unique_ptr<sim::LaboratoryRunner> lab_worker_;
    std::shared_ptr<const sim::LaboratorySnapshot> lab_snapshot_;
    std::shared_ptr<const std::vector<sim::Color>> last_lab_pixels_;
    sim::FixedClock clock_;
    ui::LaboratoryControls lab_controls_;
    std::optional<sim::Recording> recording_;
    std::vector<sim::Color> pixels_ = std::vector<sim::Color>(sim::World::cell_count);
    double step_ms_{};
    unsigned uploads_{};
    bool smoke_single_step_{}, smoke_lab_ok_{true};
    std::uint64_t smoke_paused_tick_{};
    sim::PhysicsConfig physics_;
    game::GameSession game_;
    game::InputBuffer game_input_;
    sim::FixedClock game_clock_;
    unsigned game_uploads_{}, smoke_game_frame_{};
};

} // namespace

std::expected<LaunchOptions, std::string> parse_options(int argc, char** argv) {
    LaunchOptions result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--smoke-test") result.smoke_test = true;
        else if(argument=="--lab-smoke-test") result.laboratory_smoke_test=true;
        else if (argument == "--laboratory") result.start_in_laboratory = true;
        else if (argument == "--arena") result.start_in_arena = true;
        else if(argument=="--physics-threads"&&i+1<argc) {
            const std::string_view value{argv[++i]};
            if(value.size()!=1||value.front()<'1'||value.front()>'4')
                return std::unexpected{"Liczba wątków fizyki musi wynosić 1–4."};
            result.physics_threads=static_cast<unsigned>(value.front()-'0');
        }
        else if (argument == "--profile" && i + 1 < argc) result.profile = utf8_path(argv[++i]);
        else return std::unexpected{"Nieznany albo niepełny argument: " + std::string{argument}};
    }
    if ((result.smoke_test||result.laboratory_smoke_test) && result.profile.empty())
        return std::unexpected{"Test wymaga osobnego katalogu: --smoke-test --profile <katalog>."};
    if (result.start_in_arena && result.start_in_laboratory)
        return std::unexpected{"Wybierz jeden tryb startowy: --arena albo --laboratory."};
    if(result.smoke_test&&result.laboratory_smoke_test) return std::unexpected{"Wybierz jeden rodzaj testu."};
    return result;
}

std::expected<int, std::string> run(const LaunchOptions& options) {
    SDL_SetAppMetadata("Strefa Zero", build::version.data(), "local.strefazero.prototype");
    auto system = platform::SdlSystem::create();
    if (!system) return std::unexpected{system.error()};
    auto profile = options.profile;
    if (profile.empty()) {
        using SdlString = std::unique_ptr<char, decltype(&SDL_free)>;
        SdlString path{SDL_GetPrefPath("StrefaZero", "Prototype"), SDL_free};
        if (!path) return std::unexpected{"Nie można ustalić katalogu ustawień."};
        profile = utf8_path(path.get());
    }
    platform::SettingsStore store{profile / "settings.ini"};
    if(options.smoke_test||options.laboratory_smoke_test) std::filesystem::create_directories(profile);
    auto loaded = store.load();
    std::string message = loaded ? std::string{} : loaded.error() + " Użyto ustawień domyślnych.";
    // A smoke run starts from known defaults and only writes its dedicated profile.
    auto settings = options.smoke_test||options.laboratory_smoke_test ? core::Settings{} : loaded.value_or(core::Settings{});
    if(options.laboratory_smoke_test) {settings.vsync=false;settings.frame_limit=60;}
    auto window = platform::Window::create(settings);
    if (!window) return std::unexpected{window.error()};
    settings.vsync = window->vsync_enabled();
    if (!window->warning().empty()) message = window->warning();
    const char* base = SDL_GetBasePath();
    if (!base) return std::unexpected{"Nie można ustalić katalogu aplikacji."};
    const auto assets = utf8_path(base) / "assets";
    auto gui = ui::Session::create(*window, assets);
    if (!gui) return std::unexpected{gui.error()};
    std::array<std::optional<platform::Texture>, 3> textures;
    std::array<ui::Image, 3> images{};
    constexpr std::array filenames{"forest.bmp", "industrial.bmp", "winter.bmp"};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        auto loaded_texture = platform::Texture::load_bmp(assets / "backgrounds" / filenames[i]);
        if (loaded_texture) {
            textures[i].emplace(std::move(*loaded_texture));
            images[i] = {textures[i]->id(), textures[i]->width(), textures[i]->height()};
        } else message = loaded_texture.error();
    }
    std::vector<sim::Color> blank(sim::World::cell_count);
    auto canvas = platform::Texture::create_rgba(sim::World::width, sim::World::height,
        std::as_bytes(std::span{blank}));
    if (!canvas) return std::unexpected{canvas.error()};
    auto soldier = platform::Texture::load_bmp(assets / "actors" / "soldier_pl.bmp");
    if (!soldier) return std::unexpected{soldier.error()};
    if (soldier->width() != 128 || soldier->height() != 192)
        return std::unexpected{"Atlas żołnierza wymaga rozmiaru 128 × 192."};
    auto parts = platform::Texture::load_bmp(assets/"actors"/"parts.bmp");
    auto weapons = platform::Texture::load_bmp(assets/"weapons"/"weapons.bmp");
    if (!parts || !weapons) return std::unexpected{"Brak atlasu części postaci lub broni."};
    if (!options.smoke_test&&!options.laboratory_smoke_test) window->show();
    sim::PhysicsConfig physics;physics.worker_threads=options.physics_threads;
    Application application{*window, *gui, store, settings, images, *canvas,
        {soldier->id(), soldier->width(), soldier->height()}, {parts->id(),parts->width(),parts->height()},
        {weapons->id(),weapons->width(),weapons->height()}, std::move(message),physics};
    return application.run(options.smoke_test, options.start_in_laboratory, options.start_in_arena,options.laboratory_smoke_test);
}

} // namespace strefa::app

