#include "command/action-args.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

void formatValue(std::string& out, const ActionValue& v)
{
    char buf[32];
    if (const int64_t* i = std::get_if<int64_t>(&v)) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(*i));
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
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>((*list)[k]));
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
        if (a.m_name == arg.m_name) { a.m_value = std::move(arg.m_value); return *this; }
    m_args.push_back(std::move(arg));
    return *this;
}

const ActionValue* ActionArgs::value(std::string_view name) const
{
    for (const ActionArg& a : m_args)
        if (a.m_name == name) return &a.m_value;
    return nullptr;
}

std::string ActionArgs::format() const
{
    std::string out;
    for (const ActionArg& a : m_args) {
        if (!out.empty()) out += ' ';
        out += a.m_name;
        out += '=';
        formatValue(out, a.m_value);
    }
    return out;
}

void ActionArgs::append(const ActionArgs& more)
{
    for (const ActionArg& a : more.m_args) add(a);
}

// ---------------------------------------------------------------- specs

std::string checkArgs(std::span<const ArgSpec> specs, const ActionArgs& args)
{
    for (const ActionArg& a : args.all())
        if (std::none_of(specs.begin(), specs.end(), [&](const ArgSpec& s) { return s.name == a.name(); }))
            return "unknown argument " + quoted(a.name());
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
            if (d && !std::isfinite(*d)) return quoted(s.name) + " isn't finite";
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

std::string checkPayload(const std::type_info* takes, const std::any& payload)
{
    if (!takes) return payload.has_value() ? "it takes no payload" : std::string();
    if (!payload.has_value()) return "its payload is missing";
    return payload.type() == *takes ? std::string() : "its payload is of the wrong type";
}
