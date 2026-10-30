#pragma once
// The add-node menu: a focused search field over the node kinds that fit (node-kinds.hpp). Enter
// or click picks.

#include "node/node-kinds.hpp"

class NodeMenu {
public:
    enum class Fit : uint8_t {
        Any,          // free placement
        NeedsInput,   // it will be fed: from a dragged output, or inserted into a link
        NeedsOutput,  // it will feed a dragged input
    };

    void open(Fit fit);
    // Draws the menu while it is open. True when the user picked a type (the menu closes).
    bool draw(NodeType& picked);
    bool isOpen() const { return m_open; }

private:
    char m_search[64] = "";
    int  m_highlight  = 0;
    Fit  m_fit        = Fit::Any;
    bool m_open       = false;
    bool m_justOpened = false;
};
