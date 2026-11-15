#pragma once
// Unsaved changes, asked about before they're lost: closing a project, or quitting with some. One
// question at a time, a modal: "Save changes to <name> before closing / quitting?"
//   Yes     saved (asked where, if it has no file yet), then closed, or on to the next
//   No      closed unsaved; quitting: left as it is, on to the next
//   Cancel  nothing closes, no quitting
// It saves and closes through actions; the interface tells it how a save went and when a project goes.
//
//   unsaved.askToClose(id);   // file.close on an unsaved project
//   unsaved.askToQuit();      // app.quit: quit() once every unsaved one is answered
//   unsaved.saved(id, ok);    // a project.save finished (or its dialog was cancelled)
//   unsaved.closing(id);      // a project is going
//   unsaved.draw();           // once a frame

#include "command/actions.hpp"
#include "project/projects-manager.hpp"

#include <optional>
#include <vector>

struct InputFrame;

class UnsavedChanges {
public:
    void init(const ProjectsManager& projects, ActionRegistry& actions, const InputFrame& input);

    void askToClose(ProjectId project);   // nothing while a question is out
    void askToQuit();                     // ... the same; with nothing unsaved, quit() at once
    void saved(ProjectId project, bool ok);
    void closing(ProjectId project);
    void draw();
    bool quit() const { return m_quit; }   // every unsaved one answered: the app may go

private:
    struct Confirm { bool quitting = false; ProjectId project = kNoProject; };
    void askNextBeforeQuit();   // the next unsaved one not answered yet; none left: quit
    void answered(bool saved);  // its "Yes" saved it (or didn't): on to what was waiting
    void stop();                // Cancel: nothing closes, no quitting

    const ProjectsManager* m_projects = nullptr;
    ActionRegistry*        m_actions  = nullptr;
    const InputFrame*      m_input    = nullptr;
    std::optional<Confirm> m_confirm;   // asked now
    std::optional<Confirm> m_saving;    // answered Yes: waits for its save
    bool m_quitting = false;            // asking about each unsaved one in turn, then quitting
    std::vector<ProjectId> m_keptUnsaved;   // ... answered No so far
    bool m_quit = false;
};
