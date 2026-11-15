#include "project/projects-manager-actions.hpp"
#include "project/projects-manager.hpp"

using namespace ProjectArgs;

void registerProjectsManagerActions(ActionRegistry& actions, ProjectsManager& projects)
{
    using K = ActionKind;
    const auto settled = [&projects] {   // nothing going on in the active one (or none open)
        if (projects.empty()) return true;
        const Project& p = projects.active();
        const Workspace& w = p.workspace();
        return !w.strokeActive() && !w.preview() && !p.actions().anyGesture();   // a scrub has no preview until it moves
    };

    actions.add({ .id = Actions::ProjectNew, .title = "New", .kind = K::AppState, .enabled = settled,
                  .run = [&projects](const ActionArgs&) -> ActionResult { return { true, int64_t(projects.create().id()) }; } });
    actions.add({ .id = Actions::ProjectClose, .title = "Close", .kind = K::AppState,
                  .args = { Arg::id(kProject).optional(), Arg::flag(kDiscard).optional() }, .enabled = settled, .run = [&projects](const ActionArgs& a) {
        const Project* project = projects.find(a.get(kProject, projects.activeId()));
        if (!project || (project->unsaved() && !a.get(kDiscard, false))) return false;
        return projects.close(project->id());
    } });
    actions.add({ .id = Actions::ProjectSave, .title = "Save", .kind = K::AppState,
                  .args = { Arg::id(kProject).optional(), Arg::text(kPath).optional() }, .enabled = settled,
                  .run = [&projects](const ActionArgs& a) -> ActionResult {
        Project* project = projects.find(a.get(kProject, projects.activeId()));
        if (!project) return ActionResult::refused("no such project");
        std::string error;
        if (!project->save(a.get(kPath, project->path()), error)) return ActionResult::refused(error);
        return { true, int64_t(project->id()) };
    } });
    actions.add({ .id = Actions::ProjectOpen, .title = "Open", .kind = K::AppState, .args = { Arg::text(kPath) }, .enabled = settled,
                  .run = [&projects](const ActionArgs& a) -> ActionResult {
        const std::string path = a.get(kPath);
        for (const auto& open : projects.all())
            if (!open->path().empty() && open->path() == path) {
                projects.activate(open->id());
                return { true, int64_t(open->id()) };
            }
        std::string error;
        std::unique_ptr<Project> project = Project::open(path, error);
        if (!project) return ActionResult::refused(error);
        return { true, int64_t(projects.add(std::move(project)).id()) };
    } });
    actions.add({ .id = Actions::ProjectActivate, .title = "Switch to", .kind = K::AppState,
                  .args = { Arg::id(kProject) }, .enabled = settled, .run = [&projects](const ActionArgs& a) { return projects.activate(a.get(kProject)); } });
}
