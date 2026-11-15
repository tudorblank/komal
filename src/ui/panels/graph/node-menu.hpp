#pragma once
// The add-node menu: a focused search field over the node kinds that fit (node-kinds.hpp). Enter
// or click picks; the arrows move the highlight (menu.up / .down / .accept, dialog.cancel).

#include "node/node-kinds.hpp"

class ActionRegistry;
struct InputFrame;

class NodeMenu {
public:
    void open(bool needsInput);   // needsInput: it will be fed (a dragged output, a link it goes into)
    // Draws the menu while it is open. True when the user picked a type (the menu closes).
    bool draw(NodeType& picked, const ActionRegistry& actions, const InputFrame& input);

private:
    char m_search[64] = "";
    int  m_highlight  = 0;
    bool m_needsInput = false;   // every kind has an output
    bool m_open       = false;
    bool m_justOpened = false;
};
