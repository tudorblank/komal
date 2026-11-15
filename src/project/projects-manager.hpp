#pragma once
// The projects open at once (the app's tabs). With any open, one is active: the one everything
// shows and edits. Komal starts with none (the interface shows its intro then), and closing the
// last one leaves none.
//
//   ProjectsManager projects;                   // none open
//   projects.create();                          // "Untitled 1", blank, active
//   projects.create();                          // "Untitled 2", active; 1 stays open
//   projects.close(projects.active().id());     // gone, unasked: the interface asks first (Project::unsaved)
//
// What changed is told through `changed`, in this order:
//   Opened     one more (made or read), not active yet
//   Closing    one is about to go: it's still there, so listeners let go of what they made for it
//   Activated  another one is active (after Opened, or after the active one closed and others are left)
// The projects change only through their actions (project/projects-manager-actions) or a test.

#include "base/signal.hpp"
#include "project/project.hpp"

#include <cassert>
#include <memory>
#include <optional>
#include <vector>

struct ProjectsEvent {
    enum class Type : uint8_t { Opened, Closing, Activated } type;
    ProjectId id;
};

class ProjectsManager {
public:
    size_t size() const { return m_open.size(); }
    bool   empty() const { return m_open.empty(); }
    const std::vector<std::unique_ptr<Project>>& all() const { return m_open; }   // in tab order
    Project*       find(ProjectId id);
    const Project* find(ProjectId id) const;
    Project&       active()       { assert(!empty()); return *m_open[m_active]; }   // only with one open
    const Project& active() const { assert(!empty()); return *m_open[m_active]; }
    ProjectId activeId() const { return empty() ? kNoProject : active().id(); }

    Project& create();   // a new blank one, active, after the active one
    Project& add(std::unique_ptr<Project> project);   // one already open (read from a file), active, after the active one
    bool activate(ProjectId id);
    // Closed without asking. The active one closing: the one after it is active (else the one
    // before); the last one closing: none.
    bool close(ProjectId id);
    bool anyUnsaved() const;   // quitting asks first

    Signal<ProjectsManager, const ProjectsEvent&> changed;

private:
    std::optional<size_t> indexOf(ProjectId id) const;   // its place in tab order

    std::vector<std::unique_ptr<Project>> m_open;
    size_t   m_active = 0;
    uint32_t m_lastId = 0;        // ids never come back
    uint32_t m_untitled = 0;      // "Untitled n": n never comes back either
};
