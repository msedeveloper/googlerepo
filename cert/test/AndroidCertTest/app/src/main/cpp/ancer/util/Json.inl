#pragma once
#include "Json.hpp"

#include <type_traits>

#include "Error.hpp"
#include "Log.hpp"


//==================================================================================================

namespace ancer::json_detail {
    constexpr Log::Tag TAG{"ancer.json"};

    struct ToJson {
        static constexpr bool is_to_json = true;
        static constexpr bool is_from_json = false;

        nlohmann::json& j;

        template <bool, typename T>
        void Convert(const char* name, const T& value) {
            j[name] = value;
        }

        template <bool Required, size_t N, typename T>
        void ConvertEnum(const char* name, const T& value, const char* const* enum_names) noexcept {
            auto i = static_cast<size_t>(value);
            if ( ! (0 <= i && i < N) ) {
                FatalError(TAG, "Invalid enum value %zu / %zu!", i, N);
            }
            j[name] = enum_names[i];
        }

        template <bool Required, size_t N, typename T, typename StringT>
        void ConvertEnum(const char* name, const T& value, const std::array<StringT,N>& enum_names) noexcept {
            auto i = static_cast<size_t>(value);
            if ( ! (0 <= i && i < N) ) {
                FatalError(TAG, "Invalid enum value %zu / %zu!", i, N);
            }
            j[name] = enum_names[i];
        }
    };

    struct FromJson {
        static constexpr bool is_to_json = false;
        static constexpr bool is_from_json = true;

        const nlohmann::json& j;

        template <bool Required, typename T>
        void Convert(const char* name, T& value) noexcept {
            try {
                if constexpr ( Required ) {
                    j.at(name).get_to(value);
                } else {
                    if ( j.count(name)) {
                        j.at(name).get_to(value);
                    }
                }
            } catch ( const std::exception& e ) {
                FatalError(
                        TAG,
                        "An fatal error occurred when trying to read '%s %s' from JSON: %s",
                        typeid(T).name(), name, e.what());
            }
        }

        template <bool Required, size_t N, typename T>
        void ConvertEnum(const char* name, T& value, const char* const* enum_names) noexcept {
            try {
                if ( Required || j.count(name) ) {
                    std::string enum_name;
                    j.at(name).get_to(enum_name);
                    const auto* end = enum_names + N;
                    auto it = std::find(enum_names, end, enum_name);
                    if ( it == end ) {
                        FatalError(TAG, "Invalid enum name: '%s'", enum_name.c_str());
                    }
                    value = static_cast<T>(it - enum_names);
                }
            } catch ( const std::exception& e ) {
                FatalError(
                        TAG,
                        "An fatal error occurred when trying to read '%s %s' from JSON: %s",
                        typeid(T).name(), name, e.what());
            }
        }

        template <bool Required, size_t N, typename T, typename StringT>
        void ConvertEnum(const char* name, T& value, const std::array<StringT,N>& enum_names) noexcept {
            try {
                if ( Required || j.count(name) ) {
                    std::string enum_name;
                    j.at(name).get_to(enum_name);
                    auto it = std::find(std::begin(enum_names), std::end(enum_names), enum_name);
                    if ( it == std::end(enum_names) ) {
                        FatalError(TAG, "Invalid enum name: '%s'", enum_name.c_str());
                    }
                    value = static_cast<T>(it - std::begin(enum_names));
                }
            } catch ( const std::exception& e ) {
                FatalError(
                        TAG,
                        "An fatal error occurred when trying to read '%s %s' from JSON: %s",
                        typeid(T).name(), name, e.what());
            }
        }
    };
}  // namespace ancer::json_detail

//==================================================================================================

// Notes:
// - Templated T in JsonConverter::Convert to avoid const tomfoolery.
// - Yes, we'll have a 'different' JsonConverter template depending on the file/namespace Type is
//   in. That's not a problem though since we just need to be able to go "We want that class"; with
//   our JSON lib using ADL for to/from_json anyway, trying to force namespaces is uglier than just
//   going with that setup since we'd need to require this be used at global scope or some other
//   tomfoolery.
#define INTERNAL_JSON_BOILERPLATE(Type) \
namespace { \
    template <typename> struct JsonConverter_; \
    template <> struct JsonConverter_<Type> { \
        template <typename Converter, typename T> \
        static auto Convert(Converter j, T&& t) \
            -> std::enable_if_t<std::is_same_v<std::decay_t<T>, Type>>; \
    }; \
}

#define INTERNAL_CONVERT_BEGIN(Type) \
template <typename Converter, typename T> \
auto JsonConverter_<Type>::Convert(Converter _converter, T&& data) \
    -> std::enable_if_t<std::is_same_v<std::decay_t<T>, Type>>

//--------------------------------------------------------------------------------------------------

#define INTERNAL_TO_JSON(Type) \
inline void to_json(nlohmann::json& j, const Type& data) \
{ JsonConverter_<Type>::Convert(ancer::json_detail::ToJson{j}, data); }

#define INTERNAL_FROM_JSON(Type) \
inline void from_json(const nlohmann::json& j, Type& data) \
{ JsonConverter_<Type>::Convert(ancer::json_detail::FromJson{j}, data); }
