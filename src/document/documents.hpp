#pragma once
// The documents open at once (the app's tabs). There's always one, and one is active: the one
// everything shows and edits. Komal starts on a blank slate (Document::blank): making or opening
// another takes its place, with no questions; once it's edited it stays, beside the others.
//
//   Documents docs;                          // "Untitled 1", blank, active
//   docs.create();                           // still that one: it's blank
//   docs.active().actions().run(...);        // edited: no longer blank
//   docs.create();                           // "Untitled 2", active; 1 stays open
//   docs.close(docs.active().id());          // gone, unasked: the interface asks first (Document::unsaved)
//
// What changed is told through `changed`, in this order:
//   Opened     one more (made or read), not active yet
//   Closing    one is about to go: it's still there, so listeners let go of what they made for it
//   Activated  another one is active (after Opened, or after the active one closed)
// The documents change only through their actions (document/documents-actions) or a test.

#include "base/signal.hpp"
#include "document/document.hpp"

#include <memory>
#include <vector>

struct DocumentsEvent {
    enum class Type : uint8_t { Opened, Closing, Activated } type;
    DocumentId id;
};

class Documents {
public:
    Documents();   // a blank slate, active

    size_t size() const { return m_open.size(); }
    const std::vector<std::unique_ptr<Document>>& all() const { return m_open; }   // in tab order
    Document*       find(DocumentId id);
    const Document* find(DocumentId id) const;
    Document&       active()       { return *m_open[m_active]; }
    const Document& active() const { return *m_open[m_active]; }

    Document& create();   // a new blank one, active (the active one is blank already: it stays)
    // One read from a file, active: in place of the active one if that's a blank slate, else after it.
    Document& open(std::unique_ptr<Document> doc);
    bool activate(DocumentId id);
    // Closed without asking. The active one closing: the one after it is active (else the one
    // before); the last one closing: a new blank slate.
    bool close(DocumentId id);
    bool anyUnsaved() const;   // quitting asks first

    Signal<Documents, const DocumentsEvent&> changed;

private:
    Document& place(std::unique_ptr<Document> doc);   // given an id, active: over a blank active one, else after it

    std::vector<std::unique_ptr<Document>> m_open;
    size_t   m_active = 0;
    uint32_t m_lastId = 0;        // ids never come back
    uint32_t m_untitled = 0;      // "Untitled n": n never comes back either
};
