#include "command/action-args.hpp"

#include <algorithm>
#include <cstdio>

namespace {

void formatValue(std::string& out, const ActionValue& v)
{
    char buf[32];
    if (const int64_t* i = std::get_if<int64_t>(&v)) {
        std::snprintf(buf, sizeof buf, "%lld", (long long)*i);
        out += buf;
    } else if (const double* d = std::get_if<double>(&v)) {
        std::snprintf(buf, sizeof buf, "%g", *d);
        out += buf;
    } else if (const bool* b = std::get_if<bool>(&v)) {
        out += *b ? "on" : "off";
    } else if (const std::string* s = std::get_if<std::string>(&v)) {
        const bool quote = s->empty() || s->find(' ') != std::string::npos;
        if (quote) out += '"';
        out += *s;
        if (quote) out += '"';
    } else if (const auto* list = std::get_if<std::vector<int64_t>>(&v)) {
        out += '[';
        for (size_t k = 0; k < list->size(); ++k) {
            if (k) out += ',';
            std::snprintf(buf, sizeof buf, "%lld", (long long)(*list)[k]);
            out += buf;
        }
        out += ']';
    }
}

std::string quoted(std::string_view name) { return "'" + std::string(name) + "'"; }

}

// ---------------------------------------------------------------- arguments

ActionArgs& ActionArgs::add(ActionArg arg)
{
    for (ActionArg& a : m_args)
        if (a.name == arg.name) { a.value = std::move(arg.value); return *this; }
    m_args.push_back(std::move(arg));
    return *this;
}

const ActionValue* ActionArgs::value(std::string_view name) const
{
    for (const ActionArg& a : m_args)
        if (a.name == name) return &a.value;
    return nullptr;
}

int64_t ActionArgs::integer(std::string_view name, int64_t fallback) const
{
    const ActionValue* v = value(name);
    const int64_t* i = v ? std::get_if<int64_t>(v) : nullptr;
    return i ? *i : fallback;
}

double ActionArgs::number(std::string_view name, double fallback) const
{
    const ActionValue* v = value(name);
    if (!v) return fallback;
    if (const double* d = std::get_if<double>(v)) return *d;
    if (const int64_t* i = std::get_if<int64_t>(v)) return double(*i);
    return fallback;
}

bool ActionArgs::flag(std::string_view name, bool fallback) const
{
    const ActionValue* v = value(name);
    const bool* b = v ? std::get_if<bool>(v) : nullptr;
    return b ? *b : fallback;
}

std::string ActionArgs::text(std::string_view name) const
{
    const ActionValue* v = value(name);
    const std::string* s = v ? std::get_if<std::string>(v) : nullptr;
    return s ? *s : std::string();
}

std::vector<int64_t> ActionArgs::list(std::string_view name) const
{
    const ActionValue* v = value(name);
    const auto* l = v ? std::get_if<std::vector<int64_t>>(v) : nullptr;
    return l ? *l : std::vector<int64_t>();
}

std::string ActionArgs::format() const
{
    std::string out;
    for (const ActionArg& a : m_args) {
        if (!out.empty()) out += ' ';
        out += a.name;
        out += '=';
        formatValue(out, a.value);
    }
    return out;
}

void ActionArgs::append(const ActionArgs& more)
{
    for (const ActionArg& a : more.m_args) add(a);
}

// ---------------------------------------------------------------- specs

namespace Arg {

ArgSpec integer(std::string_view name, double min, double max) { return { name, ArgSpec::Type::Integer, true, min, max, {} }; }
ArgSpec id(std::string_view name) { return integer(name, 1.0, double(UINT32_MAX)); }
ArgSpec number(std::string_view name, double min, double max) { return { name, ArgSpec::Type::Number, true, min, max, {} }; }
ArgSpec flag(std::string_view name) { return { name, ArgSpec::Type::Flag, true, -kAny, kAny, {} }; }
ArgSpec text(std::string_view name) { return { name, ArgSpec::Type::Text, true, -kAny, kAny, {} }; }
ArgSpec choice(std::string_view name, std::vector<std::string_view> choices) { return { name, ArgSpec::Type::Text, true, -kAny, kAny, std::move(choices) }; }
ArgSpec ids(std::string_view name) { return { name, ArgSpec::Type::Ids, true, -kAny, kAny, {} }; }

}

std::string checkArgs(std::span<const ArgSpec> specs, const ActionArgs& args)
{
    for (const ActionArg& a : args.all())
        if (std::none_of(specs.begin(), specs.end(), [&](const ArgSpec& s) { return s.name == a.name; }))
            return "unknown argument " + quoted(a.name);
    for (const ArgSpec& s : specs) {
        const ActionValue* v = args.value(s.name);
        if (!v) {
            if (s.required) return "missing " + quoted(s.name);
            continue;
        }
        const auto inRange = [&](double d) { return d >= s.min && d <= s.max; };   // NaN fails
        switch (s.type) {
        case ArgSpec::Type::Integer: {
            const int64_t* i = std::get_if<int64_t>(v);
            if (!i) return quoted(s.name) + " isn't an integer";
            if (!inRange(double(*i))) return quoted(s.name) + " is out of range";
            break;
        }
        case ArgSpec::Type::Number: {
            const double* d = std::get_if<double>(v);
            const int64_t* i = std::get_if<int64_t>(v);
            if (!d && !i) return quoted(s.name) + " isn't a number";
            if (!inRange(d ? *d : double(*i))) return quoted(s.name) + " is out of range";
            break;
        }
        case ArgSpec::Type::Flag:
            if (!std::holds_alternative<bool>(*v)) return quoted(s.name) + " isn't on / off";
            break;
        case ArgSpec::Type::Text: {
            const std::string* t = std::get_if<std::string>(v);
            if (!t) return quoted(s.name) + " isn't text";
            if ((!s.choices.empty() && std::find(s.choices.begin(), s.choices.end(), *t) == s.choices.end()) || (s.accepts && !s.accepts(*t)))
                return quoted(s.name) + " can't be \"" + *t + "\"";
            break;
        }
        case ArgSpec::Type::Ids: {
            const auto* l = std::get_if<std::vector<int64_t>>(v);
            if (!l) return quoted(s.name) + " isn't a list of ids";
            if (std::any_of(l->begin(), l->end(), [](int64_t id) { return id < 1; })) return quoted(s.name) + " holds a non-id";
            break;
        }
        }
    }
    return {};
}
