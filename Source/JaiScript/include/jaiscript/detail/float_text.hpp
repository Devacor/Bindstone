#pragma once

#ifndef __JAISCRIPT_DETAIL_FLOAT_TEXT_HPP__
#define __JAISCRIPT_DETAIL_FLOAT_TEXT_HPP__

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace jai::detail {

    // strtod with the range verdict every C runtime agrees on: glibc and MinGW's CRT raise ERANGE for any
    // subnormal result (5e-324) while MSVC's does not, so an ERANGE parse is out of range only when it
    // produced zero or infinity (underflow past the smallest subnormal, overflow)
    struct float_text_result {
        double value = 0.0;
        const char* end = nullptr;
        bool out_of_range = false;
    };

    inline float_text_result parse_float_text(const char* text) noexcept {
        const int saved = errno;
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        const bool out_of_range = errno == ERANGE && (value == 0.0 || std::isinf(value));
        errno = saved;
        return { value, end, out_of_range };
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_FLOAT_TEXT_HPP__
