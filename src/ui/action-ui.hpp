#pragma once
// Menu entries made from registered actions (core/actions.h): the title, shortcut, enabled
// state and check mark all come from the action, and picking the entry runs it.

#include "core/actions.hpp"

#include <string_view>

void actionMenuItem(ActionRegistry& actions, std::string_view id);
