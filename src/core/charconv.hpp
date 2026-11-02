#pragma once

#include <charconv>
#include <system_error>
#include <type_traits>
#include <version>

#if !defined(__cpp_lib_to_chars)
#include <locale>
#include <sstream>
#include <string>
#endif

namespace strefa::core {

// std::from_chars. libc++ before LLVM 20 has no floating-point overloads (__cpp_lib_to_chars is undefined), so
// there floats take a locale-independent fallback with the chars_format::general decimal grammar. The fallback
// rejects inf/nan text, which every caller rejects as non-finite anyway; libstdc++ and MSVC never use it.
template<class T> std::from_chars_result from_chars(const char* first, const char* last, T& value) {
#if defined(__cpp_lib_to_chars)
    return std::from_chars(first, last, value);
#else
    if constexpr (!std::is_floating_point_v<T>) {
        return std::from_chars(first, last, value);
    } else {
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
        T parsed{};
        stream >> parsed;
        if (stream.fail()) return {p, std::errc::result_out_of_range};
        value = parsed;
        return {p, std::errc{}};
    }
#endif
}

} // namespace strefa::core
