// The projects open at once: each a workspace with its own actions and history, and its file.
// None open at first, none after the last one closes; a new one is always new. Unsaved: its
// history isn't at the step its file holds.

#include "helpers.hpp"
#include "project/projects-manager.hpp"
#include "project/projects-manager-actions.hpp"

#include <algorithm>

namespace {

// One edit, through the project's own actions: a step of its own history.
void edit(Project& project)
{
    REQUIRE(project.actions().run(Actions::NodeAdd, { { ProjectArgs::kType, "move" }, { ProjectArgs::kX, 0.0f }, { ProjectArgs::kY, 0.0f } }));
}

std::unique_ptr<Project> fromFile(const std::string& path)   // as the file format will hand one over
{
    auto project = std::make_unique<Project>();
    project->workspace().init();
    project->savedAs(path);
    return project;
}

}

TEST_CASE("a blank slate: never edited, no file; an edit makes it a project, unsaved")
{
    Project project;
    project.workspace().init();
    CHECK(project.blank());
    CHECK_FALSE(project.unsaved());
    edit(project);
    CHECK_FALSE(project.blank());
    CHECK(project.unsaved());
    REQUIRE(project.actions().run(Actions::EditUndo));   // back to how it began: nothing to lose ...
    CHECK_FALSE(project.unsaved());
    CHECK_FALSE(project.blank());                        // ... but it was edited
}

TEST_CASE("unsaved: away from the step its file holds, whichever way")
{
    Project project;
    project.workspace().init();
    edit(project);
    project.savedAs("/somewhere/poster.komal");
    CHECK(project.name() == "poster");
    CHECK(project.path() == "/somewhere/poster.komal");
    CHECK_FALSE(project.unsaved());
    CHECK_FALSE(project.blank());
    REQUIRE(project.actions().run(Actions::EditUndo));   // before it
    CHECK(project.unsaved());
    REQUIRE(project.actions().run(Actions::EditRedo));   // back on it
    CHECK_FALSE(project.unsaved());
    REQUIRE(project.actions().run(Actions::EditUndo));
    edit(project);                                       // another branch
    CHECK(project.unsaved());
}

TEST_CASE("each project has its own workspace, history and actions")
{
    ProjectsManager projects;
    Project& one = projects.create();
    edit(one);
    Project& two = projects.create();
    REQUIRE(&two != &one);
    CHECK(two.blank());
    CHECK(two.workspace().history().size() == 1);
    CHECK_FALSE(two.actions().run(Actions::EditUndo).done);   // nothing of its own to undo
    CHECK(one.workspace().history().size() == 2);
    const auto logged = [](const Project& project, ActionId id) {
        const auto& log = project.actions().log();
        return std::any_of(log.begin(), log.end(), [&](const ActionLogEntry& e) { return e.id == id; });
    };
    CHECK(logged(one, Actions::NodeAdd));         // each its own log
    CHECK_FALSE(logged(two, Actions::NodeAdd));
}

TEST_CASE("none open at first; new ones are always new, opened ones go beside them")
{
    ProjectsManager projects;
    CHECK(projects.empty());
    CHECK(projects.activeId() == kNoProject);

    projects.add(fromFile("/a/cover.komal"));
    CHECK(projects.size() == 1);
    CHECK(projects.active().name() == "cover");
    projects.create();                             // as many blank ones as asked for
    projects.create();
    CHECK(projects.size() == 3);
    CHECK(projects.active().name() == "Untitled 2");
    CHECK(projects.active().blank());
    projects.add(fromFile("/a/back.komal"));      // beside them, none replaced
    CHECK(projects.size() == 4);
    CHECK(projects.active().name() == "back");
    CHECK(projects.all()[0]->name() == "cover");
}

TEST_CASE("activating and closing: the one after, else before; never none open")
{
    ProjectsManager projects;
    edit(projects.create());
    const ProjectId a = projects.active().id();
    edit(projects.create());
    const ProjectId b = projects.active().id();
    edit(projects.create());
    const ProjectId c = projects.active().id();
    CHECK(projects.anyUnsaved());

    REQUIRE(projects.activate(b));
    CHECK_FALSE(projects.activate(ProjectId{ 99 }));
    REQUIRE(projects.close(b));                    // the active one: the one after it
    CHECK(projects.active().id() == c);
    REQUIRE(projects.close(c));                    // the last in the row: the one before it
    CHECK(projects.active().id() == a);
    REQUIRE(projects.activate(a));
    REQUIRE(projects.close(a));                    // the only one: none left
    CHECK(projects.empty());
    CHECK(projects.activeId() == kNoProject);
    CHECK_FALSE(projects.anyUnsaved());
    CHECK_FALSE(projects.close(a));
    CHECK(projects.create().name() == "Untitled 4");   // names never come back
}

TEST_CASE("closing one before the active one keeps the active one active")
{
    ProjectsManager projects;
    edit(projects.create());
    const ProjectId a = projects.active().id();
    edit(projects.create());
    const ProjectId b = projects.active().id();
    REQUIRE(projects.close(a));
    CHECK(projects.active().id() == b);
}

TEST_CASE("the projects tell what changed: opened, closing (still there), activated")
{
    ProjectsManager projects;
    std::vector<std::pair<ProjectsEvent::Type, ProjectId>> heard;
    bool closingStillThere = true;
    const Connection c = projects.changed.connect([&](const ProjectsEvent& e) {
        heard.push_back({ e.type, e.id });
        if (e.type == ProjectsEvent::Type::Closing) closingStillThere &= projects.find(e.id) != nullptr;
    });
    using T = ProjectsEvent::Type;
    projects.add(fromFile("/a/cover.komal"));
    const ProjectId cover = projects.active().id();
    CHECK(heard == std::vector<std::pair<T, ProjectId>>{ { T::Opened, cover }, { T::Activated, cover } });
    heard.clear();
    const ProjectId second = projects.create().id();   // a new one beside it
    CHECK(heard == std::vector<std::pair<T, ProjectId>>{ { T::Opened, second }, { T::Activated, second } });
    heard.clear();
    projects.activate(second);                          // already: nothing
    projects.activate(cover);
    CHECK(heard == std::vector<std::pair<T, ProjectId>>{ { T::Activated, cover } });
    heard.clear();
    projects.close(cover);                              // the active one: the next one takes over
    CHECK(heard == std::vector<std::pair<T, ProjectId>>{ { T::Closing, cover }, { T::Activated, second } });
    heard.clear();
    projects.close(second);                             // the last one: none left, none active
    CHECK(heard == std::vector<std::pair<T, ProjectId>>{ { T::Closing, second } });
    CHECK(closingStillThere);
}

TEST_CASE("the projects' actions: unsaved work is never closed unasked")
{
    ProjectsManager projects;
    ActionRegistry actions;
    registerProjectsManagerActions(actions, projects);
    CHECK_FALSE(actions.run(Actions::ProjectClose));                                   // none open: nothing to close
    const ProjectId blank = actions.run(Actions::ProjectNew).madeAs<ProjectId>();
    const ActionResult second = actions.run(Actions::ProjectNew);                  // a new one, though one is blank
    CHECK(second.madeAs<ProjectId>() == projects.active().id());
    CHECK(projects.size() == 2);
    REQUIRE(actions.run(Actions::ProjectClose, { { ProjectArgs::kProject, blank } }));
    CHECK(projects.size() == 1);
    edit(projects.active());
    const ProjectId edited = projects.active().id();
    const ActionResult made = actions.run(Actions::ProjectNew);
    REQUIRE(made);
    CHECK(projects.size() == 2);
    REQUIRE(actions.run(Actions::ProjectActivate, { { ProjectArgs::kProject, edited } }));
    CHECK_FALSE(actions.run(Actions::ProjectClose));                                   // unsaved: refused
    CHECK(projects.size() == 2);
    REQUIRE(actions.run(Actions::ProjectClose, { { ProjectArgs::kDiscard, true } }));    // told to: gone
    CHECK(projects.size() == 1);
    CHECK(projects.active().id() == made.madeAs<ProjectId>());
    CHECK(actions.run(Actions::ProjectClose, { { ProjectArgs::kProject, made.madeAs<ProjectId>() } }));   // blank: nothing to lose
    CHECK(projects.empty());
    CHECK_FALSE(actions.run(Actions::ProjectActivate, { { ProjectArgs::kProject, ProjectId{ 99 } } }));
}
