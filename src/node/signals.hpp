#pragma once
// A node's signals and their channels, by name (node.hpp says what they do).

#include <cstdint>

enum class NodeSignal  : uint8_t { Main, Dry, Wet };
enum class NodeChannel : uint8_t { A, R, G, B };
constexpr int kSignalCount  = 3;
constexpr int kChannelCount = 4;
// Their names, once: as actions write them ("main", "a") and as the graph shows them ("Main", "A").
const char* signalId(NodeSignal s);
const char* signalLabel(NodeSignal s);
const char* channelId(NodeChannel c);
const char* channelLabel(NodeChannel c);
