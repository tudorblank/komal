#pragma once
// One open project: its workspace, the registry its actions live in (the document's own,
// registered from the start, each bound to this workspace), and its file. Anything can drive it
// without an interface: tests, a script. The app keeps the open ones in Documents.
//
//   Document doc;
//   doc.workspace().init();
//   doc.actions().run(Actions::NodeAdd, { { DocArgs::kType, "move" }, { DocArgs::kX, 0.0f }, { DocArgs::kY, 0.0f } });
//
// Its file: where it was opened from or last saved to, and the history step it held then.
//   blank    never edited and no file: a blank slate, nothing to lose
//   unsaved  its history isn't at the step its file holds (undone back to it: saved again);
//            closing it asks first. A blank slate never is.
//
// Its file is a .komal (document/workspace-file.hpp): save() writes it whole, open() reads one.

#include "command/actions.hpp"
#include "document/document-actions.hpp"
#include "document/workspace.hpp"

#include <memory>
#include <string>

enum class DocumentId : uint32_t {};   // given by Documents, never reused
constexpr DocumentId kNoDocument{};

class Document {
public:
    explicit Document(std::string name = "Untitled") : m_name(std::move(name)) { registerDocumentActions(m_actions, m_workspace); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Workspace&        workspace()       { return m_workspace; }
    const Workspace&  workspace() const { return m_workspace; }
    ActionRegistry&       actions()       { return m_actions; }
    const ActionRegistry& actions() const { return m_actions; }

    DocumentId id() const { return m_id; }
    const std::string& name() const { return m_name; }   // its file's, else what it was given ("Untitled 1")
    const std::string& path() const { return m_path; }   // empty: never saved
    bool blank() const   { return m_path.empty() && m_workspace.history().size() == 1; }
    bool unsaved() const { return m_workspace.history().current() != m_savedStep; }
    // Its workspace is in the file at `path` now, as it is (written there, or read from it).
    void savedAs(const std::string& path);

    static constexpr const char* kExtension = ".komal";
    static bool isFileName(const std::string& path);   // a .komal file, by its name (any case: Poster.KOMAL too)
    // Written to `path` (given without the extension: it's added), whole or not at all: a save cut
    // short leaves the file as it was. Then that's its file. False, with why.
    bool save(std::string path, std::string& error);
    // One read from a .komal file, as it was saved (nothing to undo). Null, with why: not a
    // project, made by a newer Komal, damaged, or not readable.
    static std::unique_ptr<Document> open(const std::string& path, std::string& error);

private:
    friend class Documents;   // gives the id

    Workspace      m_workspace;   // first: the actions point into it
    ActionRegistry m_actions;
    DocumentId  m_id = kNoDocument;
    std::string m_name;
    std::string m_path;
    StepId      m_savedStep = kStartStep;
};
