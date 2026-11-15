#include "ui/file-intake.hpp"
#include "base/frames.hpp"
#include "base/log.hpp"
#include "project/project-actions.hpp"
#include "project/projects-manager-actions.hpp"
#include "ui/panels/canvas/canvas-panel.hpp"
#include "ui/panels/graph/graph-panel.hpp"

#include <imgui_internal.h>   // FindHoveredWindowEx

void FileIntake::init(const CanvasPanel& canvas, const GraphPanel& graph)
{
    m_canvas = &canvas;
    m_graph  = &graph;
}

void FileIntake::open(std::string path)
{
    if (!Project::isFileName(path)) {
        m_importer.open(std::move(path));   // wakes the loop itself
        return;
    }
    {
        const std::lock_guard lock(m_inboxLock);
        m_toOpen.push_back(std::move(path));
    }
    wakeMainLoop();   // it may be asleep
}

void FileIntake::saveTo(ProjectId project, std::string path)
{
    {
        const std::lock_guard lock(m_inboxLock);
        m_toSave.push_back({ project, std::move(path) });
    }
    wakeMainLoop();
}

std::vector<ProjectId> FileIntake::takeInbox(ActionRegistry& actions)
{
    std::vector<std::string> open;
    std::vector<std::pair<ProjectId, std::string>> save;
    {
        const std::lock_guard lock(m_inboxLock);
        open.swap(m_toOpen);
        save.swap(m_toSave);
    }
    for (std::string& path : open) actions.run(Actions::ProjectOpen, { { ProjectArgs::kPath, std::move(path) } });
    std::vector<ProjectId> cancelled;
    for (auto& [project, path] : save) {
        if (path.empty()) {   // the dialog was cancelled: not saved
            cancelled.push_back(project);
            continue;
        }
        actions.run(Actions::ProjectSave, { { ProjectArgs::kProject, project }, { ProjectArgs::kPath, std::move(path) } });
    }
    return cancelled;
}

void FileIntake::dispatchDrops(ActionRegistry& actions)
{
    for (Drop& d : m_drops) {
        ImGuiWindow* top = nullptr;
        ImGuiWindow* underMoving = nullptr;
        ImGui::FindHoveredWindowEx(d.pos, false, &top, &underMoving);   // the window in front at that point
        Vec2 world;
        ImVec2 nodePos;
        if (Project::isFileName(d.path))   // a project: opened, wherever it lands
            actions.run(Actions::ProjectOpen, { { ProjectArgs::kPath, std::move(d.path) } });
        else if (m_canvas->dropTarget(top, d.pos, world))
            m_importer.open(std::move(d.path), ImageImporter::OnCanvas{ world.x, world.y });
        else if (m_graph->dropTarget(top, d.pos, nodePos))
            m_importer.open(std::move(d.path), ImageImporter::InGraph{ nodePos.x, nodePos.y });
        else
            Log::info("drop", "ignored (not on the canvas or the graph): %s", d.path.c_str());
    }
    m_drops.clear();
}
