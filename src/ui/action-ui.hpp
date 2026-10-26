#pragma once
// A menu entry from a registered action: title, shortcut, enabled and checked come from it.

#include "core/actions.hpp"

#include <string_view>

void actionMenuItem(ActionRegistry& actions, std::string_view id);
