#pragma once
// The document and every way to change it: the project, and the registry its actions live in
// (the document's own registered from the start). The app owns one; the interface adds its
// actions to the same registry. Anything can drive it without an interface: tests, a script.
//
//   Workspace workspace;
//   workspace.project().init();
//   workspace.actions().run(Actions::NodeAdd, { { DocArgs::kType, "move" }, { DocArgs::kX, 0.0f }, { DocArgs::kY, 0.0f } });

#include "command/actions.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"

class Workspace {
public:
    Workspace() { registerDocumentActions(m_actions, m_project); }
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    Project&        project()       { return m_project; }
    const Project&  project() const { return m_project; }
    ActionRegistry& actions()       { return m_actions; }

private:
    Project m_project;   // first: the actions point into it
    ActionRegistry m_actions;
};