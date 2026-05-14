// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <charconv>
#include <limits>
#include <string>
#include <type_traits>

namespace PhotonCommon {

#define PHOTON_STR(str) u8##str

    using CharType = char8_t;
#if defined(__cpp_lib_char8_t)
    using StringType = std::u8string;
    using StringViewType = std::u8string_view;
#else
    using StringType = std::basic_string<char8_t>;
    using StringViewType = std::basic_string_view<char8_t>;
#endif

    template<typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    StringType to_string_type(T value) {
        constexpr size_t max_digits = std::numeric_limits<T>::digits10 + 2;
        CharType buffer[max_digits];

        char* begin = reinterpret_cast<char*>(buffer);
        std::to_chars_result result = std::to_chars(begin, begin + max_digits, value);

        return StringType(buffer, reinterpret_cast<CharType*>(result.ptr));
    }

    template<typename T, std::enable_if_t<std::is_same_v<T, bool>, int> = 0>
    StringType to_string_type(T value) {
        return value ? u8"True" : u8"False";
    }

    inline StringType& to_string_type(StringType& value) {
        return value;
    }

    inline StringType to_string_type(const CharType* value) {
        return StringType(value);
    }
}

