#include <meta>
#include <string_view>

struct SettingsProbe {
    bool vsync{true};
};
static_assert(std::meta::identifier_of(^^SettingsProbe) == std::string_view{"SettingsProbe"});

int main() {
    return 0;
}
