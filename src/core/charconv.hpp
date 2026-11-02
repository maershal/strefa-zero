#pragma once

#include <charconv>
#include <system_error>
#include <type_traits>
#include <version>

#if !defined(__cpp_lib_to_chars)
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#endif

namespace strefa::core {

#if !defined(__cpp_lib_to_chars)
namespace detail {
// libc++ before LLVM 20 has no floating-point std::from_chars (__cpp_lib_to_chars is undefined). This fallback
// accepts the chars_format::general decimal grammar without inf/nan (every caller rejects non-finite values).
// It reads a long double, because libc++ num_get fails on float/double subnormals (ERANGE) where from_chars
// returns them; the narrowing may round the last bit differently from std::from_chars.
template<class T> std::from_chars_result float_from_chars(const char* first, const char* last, T& value) {
    const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    const char* p = first;
    if (p != last && *p == '-') ++p;
    bool digits = false;
    for (; p != last && is_digit(*p); ++p) digits = true;
    if (p != last && *p == '.')
        for (++p; p != last && is_digit(*p); ++p) digits = true;
    if (!digits) return {first, std::errc::invalid_argument};
    if (p != last && (*p == 'e' || *p == 'E')) {
        const char* exponent = p + 1;
        if (exponent != last && (*exponent == '+' || *exponent == '-')) ++exponent;
        if (exponent != last && is_digit(*exponent)) {
            p = exponent;
            while (p != last && is_digit(*p)) ++p;
        }
    }
    std::istringstream stream{std::string{first, p}};
    stream.imbue(std::locale::classic());
    long double wide{};
    stream >> wide;
    const auto narrow = static_cast<T>(wide);
    if (stream.fail() || !std::isfinite(narrow) || (narrow == T{} && wide != 0.0L))
        return {p, std::errc::result_out_of_range};
    value = narrow;
    return {p, std::errc{}};
}
} // namespace detail
#endif

// std::from_chars that leaves `value` untouched on every error: MSVC STL writes +-inf or 0 on a floating-point
// result_out_of_range (cf. LWG 3081). libc++ < 20 floats go through detail::float_from_chars.
template<class T> std::from_chars_result from_chars(const char* first, const char* last, T& value) {
    T parsed{};
#if defined(__cpp_lib_to_chars)
    const auto result = std::from_chars(first, last, parsed);
#else
    std::from_chars_result result{};
    if constexpr (std::is_floating_point_v<T>)
        result = detail::float_from_chars(first, last, parsed);
    else
        result = std::from_chars(first, last, parsed);
#endif
    if (result.ec == std::errc{}) value = parsed;
    return result;
}

} // namespace strefa::core
