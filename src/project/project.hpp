#pragma once
// One open project: its workspace, the registry its actions live in (the project's own,
// registered from the start, each bound to this workspace), and its file. Anything can drive it
// without an interface: tests, a script. The app keeps the open ones in ProjectsManager.
//
//   Project project;
//   project.workspace().init();
//   project.actions().run(Actions::NodeAdd, { { ProjectArgs::kType, "move" }, { ProjectArgs::kX, 0.0f }, { ProjectArgs::kY, 0.0f } });
//
// Its file: where it was opened from or last saved to, and the history step it held then.
//   blank    never edited and no file: a blank slate, nothing to lose
//   unsaved  its history isn't at the step its file holds (undone back to it: saved again);
//            closing it asks first. A blank slate never is.
//
// Its file is a .komal (project/workspace-file.hpp): save() writes it whole, open() reads one.

#include "command/actions.hpp"
#include "project/project-actions.hpp"
#include "project/workspace.hpp"

#include <memory>
#include <string>

enum class ProjectId : uint32_t {};   // given by ProjectsManager, never reused
constexpr ProjectId kNoProject{};

class Project {
public:
    explicit Project(std::string name = "Untitled") : m_name(std::move(name)) { registerProjectActions(m_actions, m_workspace); }
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    Workspace&        workspace()       { return m_workspace; }
    const Workspace&  workspace() const { return m_workspace; }
    ActionRegistry&       actions()       { return m_actions; }
    const ActionRegistry& actions() const { return m_actions; }

    ProjectId id() const { return m_id; }
    const std::string& name() const { return m_name; }   // its file's, else what it was given ("Untitled 1")
    const std::string& path() const { return m_path; }   // empty: never saved
    bool blank() const   { return m_path.empty() && m_workspace.history().size() == 1; }
    bool unsaved() const { return m_workspace.history().serial(m_workspace.history().current()) != m_savedStep; }
    // Its workspace is in the file at `path` now, as it is (written there, or read from it).
    void savedAs(const std::string& path);

    static constexpr const char* kExtension = ".komal";
    static bool isFileName(const std::string& path);   // a .komal file, by its name (any case: Poster.KOMAL too)
    // Written to `path` (given without the extension: it's added), whole or not at all: a save cut
    // short leaves the file as it was. Then that's its file. False, and error says what went wrong.
    bool save(std::string path, std::string& error);
    // One read from a .komal file, as it was saved (nothing to undo). Null, and error says what went wrong: not a
    // project, made by a newer Komal, damaged, or not readable.
    static std::unique_ptr<Project> open(const std::string& path, std::string& error);

private:
    friend class ProjectsManager;   // gives the id

    Workspace      m_workspace;   // first: the actions point into it
    ActionRegistry m_actions;
    ProjectId  m_id = kNoProject;
    std::string m_name;
    std::string m_path;
    StepSerial  m_savedStep{};   // the step its file holds (gone once deleted: unsaved until saved again)
};
