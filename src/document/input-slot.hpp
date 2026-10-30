#pragma once
// Where a link lands on a node: one type per kind of place, each holding only what it means.
// Actions write it as text ("input", "layer:2", "new:3", "mask:main.a") and read it back; text
// that isn't one of those is refused (parse() is empty).
//
//   if (const auto* layer = slot.as<InputSlot::Layer>()) ... stack->layers()[layer->index]

#include "node/node-types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

struct InputSlot {
    struct Input {                       // a one-input node's input
        bool operator==(const Input&) const = default;
    };
    struct Layer {                       // an existing stack layer: connecting replaces it
        uint32_t index;                  // 0 = bottom
        bool operator==(const Layer&) const = default;
    };
    struct NewLayer {                    // a position in a stack: connecting inserts there
        uint32_t at;                     // 0..count (count = on top)
        bool operator==(const NewLayer&) const = default;
    };
    struct Mask {                        // a channel's mask
        NodeSignal  signal;
        NodeChannel channel;
        bool operator==(const Mask&) const = default;
    };
    using Target = std::variant<Input, Layer, NewLayer, Mask>;

    Target target;

    InputSlot() = default;
    template <class T> requires std::is_constructible_v<Target, T>
    InputSlot(T t) : target(t) {}   // from any one of them

    static InputSlot input() { return Input{}; }
    static InputSlot stackLayer(uint32_t index) { return Layer{ index }; }
    static InputSlot newLayer(uint32_t at) { return NewLayer{ at }; }
    static InputSlot mask(NodeSignal s, NodeChannel c) { return Mask{ s, c }; }

    template <class T> const T* as() const { return std::get_if<T>(&target); }
    template <class T> bool is() const { return std::holds_alternative<T>(target); }
    bool operator==(const InputSlot&) const = default;

    std::string text() const;
    static std::optional<InputSlot> parse(std::string_view text);
    static bool valid(std::string_view text) { return parse(text).has_value(); }   // for an action's ArgSpec
};
