#pragma once
// The document and every way to change it: the project, and the registry its actions live in
// (the document's own registered from the start). The app owns one; the interface adds its
// actions to the same registry. Anything can drive it without an interface: tests, a script.
//
//   Editor editor;
//   editor.project().init();
//   editor.actions().run(Actions::NodeAdd, { { DocArgs::kType, "move" }, { DocArgs::kX, 0.0f }, { DocArgs::kY, 0.0f } });

#include "command/actions.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"

class Editor {
public:
    Editor() { registerDocumentActions(m_actions, m_project); }
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    Project&        project()       { return m_project; }
    const Project&  project() const { return m_project; }
    ActionRegistry& actions()       { return m_actions; }

private:
    Project        m_project;   // first: the actions point into it
    ActionRegistry m_actions;
};
