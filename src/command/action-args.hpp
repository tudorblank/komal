#pragma once
// An action's arguments: named values, written out in the action log ("from=12 to=7
// slot=layer:2"), plus an optional payload too big or too typed for the log (a stroke's points,
// an image's tiles). Every action says what it takes (ArgSpec); the registry checks that once,
// before the action runs, so a handler reads its arguments without guessing.
//
//   .args = { Arg::id("node"), Arg::flag("on") }
//   [&](const ActionArgs& a) { p.setRasterLocked(NodeId(a.integer("node")), a.flag("on")); }

#include <any>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// One named argument. Names are literals; values are numbers, flags, text or a list of ids.
using ActionValue = std::variant<int64_t, double, bool, std::string, std::vector<int64_t>>;
struct ActionArg {
    std::string_view name;
    ActionValue      value;

    template<class T>
    ActionArg(std::string_view n, const T& v) : name(n), value(toValue(v)) {}

private:
    template<class T>
    static ActionValue toValue(const T& v)
    {
        if constexpr (std::is_same_v<T, bool>)                                    return v;
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)            return int64_t(v);
        else if constexpr (std::is_floating_point_v<T>)                           return double(v);
        else if constexpr (std::is_convertible_v<const T&, std::string_view>)     return std::string(std::string_view(v));
        else {   // a list of ids (numbers, or id types)
            std::vector<int64_t> ids;
            for (const auto& id : v) ids.push_back(int64_t(id));
            return ids;
        }
    }
};

class ActionArgs {
public:
    ActionArgs() = default;
    ActionArgs(std::initializer_list<ActionArg> args) : m_args(args) {}

    ActionArgs& add(ActionArg arg);   // replaces one of the same name
    bool has(std::string_view name) const { return value(name) != nullptr; }
    const ActionValue* value(std::string_view name) const;
    const std::vector<ActionArg>& all() const { return m_args; }
    int64_t     integer(std::string_view name, int64_t fallback = 0) const;
    double      number(std::string_view name, double fallback = 0.0) const;   // an integer reads too
    bool        flag(std::string_view name, bool fallback = false) const;
    std::string text(std::string_view name) const;
    std::vector<int64_t> list(std::string_view name) const;

    std::any payload;
    template<class T> const T* payloadAs() const { return std::any_cast<T>(&payload); }

    std::string format() const;   // as the log writes them
    void append(const ActionArgs& more);   // `more`'s values win

private:
    std::vector<ActionArg> m_args;
};

// What one argument must be. None given for an action: it takes no arguments.
struct ArgSpec {
    enum class Type : uint8_t { Integer, Number, Flag, Text, Ids };
    std::string_view name;
    Type   type     = Type::Integer;
    bool   required = true;
    double min = -std::numeric_limits<double>::infinity();   // Integer, Number: inclusive
    double max =  std::numeric_limits<double>::infinity();
    std::vector<std::string_view> choices{};                  // Text: one of these (none: any text)
    bool (*accepts)(std::string_view) = nullptr;              // Text: what it must read as (none: anything)

    ArgSpec optional() const { ArgSpec s = *this; s.required = false; return s; }
    ArgSpec checked(bool (*rule)(std::string_view)) const { ArgSpec s = *this; s.accepts = rule; return s; }
};

namespace Arg {

constexpr double kAny = std::numeric_limits<double>::infinity();
ArgSpec integer(std::string_view name, double min = -kAny, double max = kAny);
ArgSpec id(std::string_view name);   // what something is called by: 1 and up
ArgSpec number(std::string_view name, double min = -kAny, double max = kAny);
ArgSpec flag(std::string_view name);
ArgSpec text(std::string_view name);
ArgSpec choice(std::string_view name, std::vector<std::string_view> choices);
ArgSpec ids(std::string_view name);   // a list of ids

}

// Why `args` don't fit `specs`: a missing, unknown or mistyped argument, or one out of range.
// Empty: they fit.
std::string checkArgs(std::span<const ArgSpec> specs, const ActionArgs& args);
