#pragma once
// Every key the app binds, in one table: which action each runs. Bound once every action is
// added (UserInterface::init); the actions themselves never name a key.

class ActionRegistry;

void bindDefaultKeys(ActionRegistry& actions);
