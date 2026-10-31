#include "node/input-slot.hpp"

#include <charconv>

std::string InputSlot::text() const
{
    if (const auto* l = as<Layer>())    return "layer:" + std::to_string(l->index);
    if (const auto* n = as<NewLayer>()) return "new:" + std::to_string(n->at);
    if (const auto* m = as<Mask>())     return std::string("mask:") + signalId(m->signal) + "." + channelId(m->channel);
    return "input";
}

std::optional<InputSlot> InputSlot::parse(std::string_view t)
{
    // A number, all of the rest of the text.
    const auto index = [&](std::string_view digits) -> std::optional<uint32_t> {
        uint32_t v = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), v);
        if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) return std::nullopt;
        return v;
    };
    if (t == "input") return input();
    if (t.starts_with("layer:")) {
        if (const auto i = index(t.substr(6))) return stackLayer(*i);
        return std::nullopt;
    }
    if (t.starts_with("new:")) {
        if (const auto i = index(t.substr(4))) return newLayer(*i);
        return std::nullopt;
    }
    if (t.starts_with("mask:")) {
        const std::string_view m = t.substr(5);
        const size_t dot = m.find('.');
        if (dot == std::string_view::npos) return std::nullopt;
        int signal = -1, channel = -1;
        for (int i = 0; i < kSignalCount; ++i)  if (m.substr(0, dot) == signalId(NodeSignal(i))) signal = i;
        for (int i = 0; i < kChannelCount; ++i) if (m.substr(dot + 1) == channelId(NodeChannel(i))) channel = i;
        if (signal < 0 || channel < 0) return std::nullopt;
        return mask(NodeSignal(signal), NodeChannel(channel));
    }
    return std::nullopt;
}
