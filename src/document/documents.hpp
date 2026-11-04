#pragma once
// The documents open at once (the app's tabs). With any open, one is active: the one everything
// shows and edits. Komal starts with none (the interface shows its intro then), and closing the
// last one leaves none.
//
//   Documents docs;                          // none open
//   docs.create();                           // "Untitled 1", blank, active
//   docs.create();                           // "Untitled 2", active; 1 stays open
//   docs.close(docs.active().id());          // gone, unasked: the interface asks first (Document::unsaved)
//
// What changed is told through `changed`, in this order:
//   Opened     one more (made or read), not active yet
//   Closing    one is about to go: it's still there, so listeners let go of what they made for it
//   Activated  another one is active (after Opened, or after the active one closed and others are left)
// The documents change only through their actions (document/documents-actions) or a test.

#include "base/signal.hpp"
#include "document/document.hpp"

#include <cassert>
#include <memory>
#include <vector>

struct DocumentsEvent {
    enum class Type : uint8_t { Opened, Closing, Activated } type;
    DocumentId id;
};

class Documents {
public:
    size_t size() const { return m_open.size(); }
    bool   empty() const { return m_open.empty(); }
    const std::vector<std::unique_ptr<Document>>& all() const { return m_open; }   // in tab order
    Document*       find(DocumentId id);
    const Document* find(DocumentId id) const;
    Document&       active()       { assert(!empty()); return *m_open[m_active]; }   // only with one open
    const Document& active() const { assert(!empty()); return *m_open[m_active]; }
    DocumentId activeId() const { return empty() ? kNoDocument : active().id(); }

    Document& create();   // a new blank one, active, after the active one
    Document& open(std::unique_ptr<Document> doc);   // one read from a file, active, after the active one
    bool activate(DocumentId id);
    // Closed without asking. The active one closing: the one after it is active (else the one
    // before); the last one closing: none.
    bool close(DocumentId id);
    bool anyUnsaved() const;   // quitting asks first

    Signal<Documents, const DocumentsEvent&> changed;

private:
    Document& place(std::unique_ptr<Document> doc);   // given an id, active, after the active one

    std::vector<std::unique_ptr<Document>> m_open;
    size_t   m_active = 0;
    uint32_t m_lastId = 0;        // ids never come back
    uint32_t m_untitled = 0;      // "Untitled n": n never comes back either
};
