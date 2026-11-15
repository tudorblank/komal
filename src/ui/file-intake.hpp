#pragma once
// Files coming in: from other threads (the OS's dialogs, the command line), dropped on the window,
// and images on their way in (ImageImporter, decoded on its workers). Each becomes an action on
// the main thread, between frames: a .komal file opens as a project, any other is imported.
//
//   intake.open(path);                 // any thread
//   intake.saveTo(project, path);      // any thread: the save dialog's answer
//   intake.drop(path, pos);            // the window's drop
//   ... each frame: takeInbox(), poll(), then after the panels: dispatchDrops()

#include "ui/image-importer.hpp"
#include "project/project.hpp"

#include <imgui.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class ActionRegistry;
class CanvasPanel;
class GraphPanel;

class FileIntake {
public:
    void init(const CanvasPanel& canvas, const GraphPanel& graph);   // where a dropped image can land

    void open(std::string path);                          // any thread
    void saveTo(ProjectId project, std::string path);     // any thread (empty: cancelled)
    void drop(std::string path, ImVec2 pos) { m_drops.push_back({ std::move(path), pos }); }

    // What came from other threads, as actions: opens and saves. Returns the saves whose dialog
    // was cancelled (not saved: whatever waited on them goes on without).
    std::vector<ProjectId> takeInbox(ActionRegistry& actions);
    // The drops, after the panels drew (their windows and rects are this frame's): a .komal
    // opens wherever it lands; an image is imported where it lands, on the canvas or the graph.
    void dispatchDrops(ActionRegistry& actions);
    bool importReady() const { return m_importer.ready(); }   // the next poll makes a node: a project must be open
    ImageImporter::Polled poll(ActionRegistry& actions) { return m_importer.poll(actions); }

private:
    const CanvasPanel* m_canvas = nullptr;
    const GraphPanel*  m_graph  = nullptr;
    ImageImporter m_importer;
    std::mutex m_inboxLock;
    std::vector<std::string> m_toOpen;                       // guarded by m_inboxLock
    std::vector<std::pair<ProjectId, std::string>> m_toSave;  // ... too
    struct Drop { std::string path; ImVec2 pos; };
    std::vector<Drop> m_drops;
};
