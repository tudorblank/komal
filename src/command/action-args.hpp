#pragma once
// An action's arguments: named values, written out in the action log ("from=12 to=7
// slot=layer:2"), plus an optional payload of bulk data the log can't hold (a stroke's points, an
// image's tiles). Every argument is an ArgKey, declared once by its action's owner: the action's
// spec, every call and every read name it, so a misspelt name doesn't compile and a value reads
// back as the type it was written as. The registry checks arguments and payload against the
// action before it runs.
//
//   inline constexpr ArgKey<NodeId> kNode{ "node" };
//   .args = { Arg::id(kNode), Arg::flag(kOn) }                // what the action takes
//   actions.run(Actions::RasterLock, { { kNode, id }, { kOn, true } });
//   [&](const ActionArgs& a) { p.setRasterLocked(a.get(kNode), a.get(kOn)); }

#include <any>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <variant>
#include <vector>

// What an argument holds, as the log writes it: a number, a flag, text, or a list of ids.
using ActionValue = std::variant<int64_t, double, bool, std::string, std::vector<int64_t>>;

// An argument's name and the type it's read and written as.
template <class T> struct ArgKey {
    std::string_view name;   // a literal: lives as long as the app
};

namespace detail {

// A type that writes itself as text and reads back from it (InputSlot, Color8).
template <class T> concept TextValue = requires(const T& v, std::string_view s) {
    { v.text() } -> std::convertible_to<std::string>;
    { T::parse(s) } -> std::same_as<std::optional<T>>;
};
template <class T> concept Whole = (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>;
template <class T> struct IdList : std::false_type {};
template <class E> requires Whole<E> struct IdList<std::vector<E>> : std::true_type {};

template <class T> ActionValue toValue(const T& v)
{
    if constexpr (std::is_same_v<T, bool>) return v;
    else if constexpr (Whole<T>) return int64_t(v);
    else if constexpr (std::is_floating_point_v<T>) return double(v);
    else if constexpr (std::is_same_v<T, std::string>) return v;
    else if constexpr (TextValue<T>) return std::string(v.text());
    else {
        static_assert(IdList<T>::value, "an argument is a number, a flag, text, a TextValue or a list of ids");
        std::vector<int64_t> ids;
        for (const auto& id : v) ids.push_back(int64_t(id));
        return ids;
    }
}

}

class ActionArg {
public:
    template <class T>
    ActionArg(ArgKey<T> key, const std::type_identity_t<T>& v) : m_name(key.name), m_value(detail::toValue<T>(v)) {}
    // A literal's text for a std::string argument: { kName, "photo" }.
    ActionArg(ArgKey<std::string> key, const char* v) : m_name(key.name), m_value(std::string(v)) {}

    std::string_view   name() const { return m_name; }
    const ActionValue& value() const { return m_value; }

private:
    friend class ActionArgs;
    std::string_view m_name;
    ActionValue      m_value;
};

class ActionArgs {
public:
    ActionArgs() = default;
    ActionArgs(std::initializer_list<ActionArg> args) : m_args(args) {}

    ActionArgs& add(ActionArg arg);   // replaces one of the same name
    template <class T> bool has(ArgKey<T> key) const { return value(key.name) != nullptr; }
    // Its value, as its key's type: only after the registry checked it against the action's
    // spec (which says it's there, unless it's optional: has() first).
    template <class T> T get(ArgKey<T> key) const;
    template <class T> T get(ArgKey<T> key, const std::type_identity_t<T>& otherwise) const { return has(key) ? get(key) : otherwise; }

    const std::vector<ActionArg>& all() const { return m_args; }
    const ActionValue* value(std::string_view name) const;
    std::string format() const;            // as the log writes them
    void append(const ActionArgs& more);   // `more`'s values win

    std::any payload;   // bulk data of the type its action declares (Action::payload)
    template <class T> const T* payloadAs() const { return std::any_cast<T>(&payload); }

private:
    std::vector<ActionArg> m_args;
};

template <class T> T ActionArgs::get(ArgKey<T> key) const
{
    const ActionValue* v = value(key.name);
    if constexpr (std::is_same_v<T, bool>) {
        const bool* b = v ? std::get_if<bool>(v) : nullptr;
        return b && *b;
    } else if constexpr (detail::Whole<T>) {
        const int64_t* i = v ? std::get_if<int64_t>(v) : nullptr;
        return T(i ? *i : 0);
    } else if constexpr (std::is_floating_point_v<T>) {
        if (const double* d = v ? std::get_if<double>(v) : nullptr) return T(*d);
        const int64_t* i = v ? std::get_if<int64_t>(v) : nullptr;   // an integer reads as a number too
        return T(i ? double(*i) : 0.0);
    } else if constexpr (std::is_same_v<T, std::string>) {
        const std::string* s = v ? std::get_if<std::string>(v) : nullptr;
        return s ? *s : std::string();
    } else if constexpr (detail::TextValue<T>) {
        const std::string* s = v ? std::get_if<std::string>(v) : nullptr;
        return s ? T::parse(*s).value_or(T{}) : T{};
    } else {
        static_assert(detail::IdList<T>::value);
        T out;
        if (const auto* l = v ? std::get_if<std::vector<int64_t>>(v) : nullptr)
            for (int64_t id : *l) out.push_back(typename T::value_type(id));
        return out;
    }
}

// What one argument must be. None given for an action: it takes no arguments.
struct ArgSpec {
    enum class Type : uint8_t { Integer, Number, Flag, Text, Ids };
    std::string_view name;
    Type   type     = Type::Integer;
    bool   required = true;
    double min = -std::numeric_limits<double>::infinity();   // Integer, Number: inclusive (a Number is finite too)
    double max =  std::numeric_limits<double>::infinity();
    std::vector<std::string_view> choices{};                  // Text: one of these (none: any text)
    bool (*accepts)(std::string_view) = nullptr;              // Text: what it must read as (none: anything)

    ArgSpec optional() const { ArgSpec s = *this; s.required = false; return s; }
};

namespace Arg {

constexpr double kAny = std::numeric_limits<double>::infinity();

template <class T> requires detail::Whole<T>
ArgSpec integer(ArgKey<T> key, double min = -kAny, double max = kAny) { return { key.name, ArgSpec::Type::Integer, true, min, max, {}, nullptr }; }
template <class T> requires detail::Whole<T>
ArgSpec id(ArgKey<T> key) { return integer(key, 1.0, double(UINT32_MAX)); }   // what something is called by: 1 and up
template <class T> requires std::is_floating_point_v<T>
ArgSpec number(ArgKey<T> key, double min = -kAny, double max = kAny) { return { key.name, ArgSpec::Type::Number, true, min, max, {}, nullptr }; }
inline ArgSpec flag(ArgKey<bool> key) { return { key.name, ArgSpec::Type::Flag, true, -kAny, kAny, {}, nullptr }; }
inline ArgSpec text(ArgKey<std::string> key) { return { key.name, ArgSpec::Type::Text, true, -kAny, kAny, {}, nullptr }; }
inline ArgSpec choice(ArgKey<std::string> key, std::vector<std::string_view> choices)
{
    return { key.name, ArgSpec::Type::Text, true, -kAny, kAny, std::move(choices), nullptr };
}
// Text that reads back as T (InputSlot "layer:2", Color8 "#FF8000FF"); anything else is refused.
template <class T> requires detail::TextValue<T>
ArgSpec parsed(ArgKey<T> key)
{
    return { key.name, ArgSpec::Type::Text, true, -kAny, kAny, {}, [](std::string_view s) { return T::parse(s).has_value(); } };
}
template <class T> requires detail::IdList<T>::value
ArgSpec ids(ArgKey<T> key) { return { key.name, ArgSpec::Type::Ids, true, -kAny, kAny, {}, nullptr }; }

}

// Why `args` don't fit `specs`: a missing, unknown or mistyped argument, or one out of range.
// Empty: they fit.
std::string checkArgs(std::span<const ArgSpec> specs, const ActionArgs& args);
// Why a payload doesn't fit what an action takes (null: none). Empty: it fits.
std::string checkPayload(const std::type_info* takes, const std::any& payload);
