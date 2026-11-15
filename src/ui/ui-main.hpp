#pragma once
// The app's interface: the menu bar, the dock layout and every panel, and where a dropped file
// goes (the panel under it). Panels never reach each other: they share the active project's
// workspace. Panels are one list (ui/panels/panel-base.hpp): their View toggles, close buttons,
// default layout, draw order, GPU work and frames all come from it.
//
// It's a shell over the open projects (ProjectsManager): it holds the app's own actions (the app's,
// the panels', the tools', the colors': none of them a project's) and hands the rest to the
// active project's registry. Switching projects switches everything to that one: the canvas
// (one per project), the graph, the navigator, the history. The colors and the tools stay:
// they're the app's. Closing an unsaved project, or quitting with one, asks first. With none
// open (at the start, after the last one closed) the bars stay and the intro takes the dock
// area: no panel of it draws.
//
// The open projects change between frames, never while one is drawn: what a frame drew with (a
// canvas's picture, a thumbnail) must last until it's shown. The project.* actions the interface runs
// ask for the change; the projects' own actions make it at the start of the next frame.
//
// The ui folder, by job:
//
//   style/     the look, defined once: theme (reads a theme file, assets/themes/), palette (every
//              color by its job, made from the theme: the one source), fonts, ui-style (ImGui's
//              theme, the sizes, each component's style), graph-style (the graph view's)
//   render/    the one way to draw: Painter (the surface), skins (each control drawn from its
//              style and its state)
//   widgets/   the controls panels are built from: what they do and where they go on a row;
//              each borrows its style and hands it to its skin
//   panels/    the windows (the canvas and the graph among them, with their engines)
//   tools/     the canvas tools: the brush, the hand
//
// Only style/ reads the palette; only render/ touches ImGui's draw lists.

#include "ui/panels/canvas/canvas-engine.hpp"
#include "base/signal.hpp"
#include "command/actions.hpp"
#include "project/projects-manager.hpp"
#include "ui/dockspace.hpp"
#include "ui/panels/intro-screen.hpp"
#include "ui/input-owner.hpp"
#include "ui/image-importer.hpp"
#include "ui/notices.hpp"
#include "ui/panels/panel-base.hpp"
#include "ui/tools/tool-manager.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CanvasPanel;
class GraphPanel;
class GpuDevice;
class Workspace;
struct InputFrame;

class UserInterface {
public:
    UserInterface();   // every panel, as each starts (open or not)

    // After ImGui + wgpu are up. It follows `projects` from then on (they outlive it).
    bool init(GpuDevice& gpu, ProjectsManager& projects, const InputFrame& input, const HostInfo& host);
    void shutdown();              // call before they go down

    // The GPU device is made again (it was lost): everything the interface made on it goes, then
    // is made on the new one.
    void releaseGpu();
    bool restoreGpu(GpuDevice& gpu);
    void notice(std::string text) { m_notices.post(std::move(text)); }   // for the user (ui/notices)

    void draw();                              // ImGui side
    void renderGpu(WGPUCommandEncoder enc);   // wgpu side (offscreen passes)

    bool quitRequested() const { return m_quit; }
    void requestQuit();   // the window's close button, the OS: app.quit (which asks if anything is unsaved)
    // When the UI next needs a frame without new input (base/frames.hpp): tiles still
    // syncing, a throttled picture or thumbnail, a hold turning into a gesture.
    Clock::Seconds nextFrameIn() const;
    // Any thread (a dialog's, the command line): a .komal file opens as a project, anything else
    // is imported as an image.
    void openFile(std::string path);
    void saveTo(ProjectId project, std::string path);   // any thread: the save dialog's answer (empty: cancelled)
    // A file dropped at window (x, y): a .komal file opens wherever it lands; an image is imported
    // if it lands on the canvas or the graph.
    void dropFile(std::string path, float x, float y) { m_drops.push_back({ std::move(path), ImVec2(x, y) }); }

private:
    template<class T> T* addPanel();
    // The bars always; the dock area's panels only with a project open (the intro's in their place).
    bool drawn(const Panel& p) const;
    void drawMenuBar();
    void registerAppActions();   // ui/app-actions.cpp: every app-state action
    const Project* projectOf(const ActionArgs& args) const;   // its kProject, else the active one (null: none)
    void dispatchDrops();
    void projectsChanged(const ProjectsEvent& e);   // the engine and every panel follow
    void registerProjectRequests();    // project.* here: each asks for its change, made between frames
    void applyProjectRequests();       // ... the changes asked for, before anything is drawn
    void takeInbox();                  // the files that came from other threads, as project requests
    void settleHistories();            // once a frame, last: history's pixels warmed and cooled (Workspace::settleHistory)
    // ---- unsaved changes: asked first, one project at a time (closing it, or quitting with it)
    void ask(ProjectId project, bool quitting);
    void askNextBeforeQuit();       // the next unsaved one not answered yet; none left: quit
    void answered(bool saved);      // its "Yes" saved it (or didn't): on to what was waiting
    void stopAsking();              // Cancel: nothing closes, no quitting
    void drawConfirm();

    ActionRegistry   m_actions;   // the app's own; the active project's through it (delegateTo)
    ProjectsManager* m_projects = nullptr;
    Connection       m_projectsEvents;
    ActionRegistry   m_projectActions;   // the projects' own (project/projects-manager-actions): logged as they run
    std::vector<std::pair<ActionId, ActionArgs>> m_projectRequests;   // asked for this frame, in order
    // Files that came from another thread (a dialog, the command line), for the next frame.
    std::mutex m_inboxLock;
    std::vector<std::string> m_toOpen;
    std::vector<std::pair<ProjectId, std::string>> m_toSave;
    ToolManager    m_tools;     // before the panels that use it
    InputOwner     m_owner;     // who has the keyboard this frame
    CanvasEngine   m_canvas;    // ... and what canvas views draw with
    std::optional<UiContext> m_context;   // what every panel reads (Panel::ui()), from init() on

    Dockspace::Panels   m_panels;      // in View menu order, also their GPU work's (the canvas first)
    std::vector<Panel*> m_drawOrder;   // Edit panels, then Show panels (PanelInfo::phase), each in menu order
    CanvasPanel* m_canvasPanel = nullptr;   // in m_panels: the ones a drop can land on
    GraphPanel*  m_graphPanel  = nullptr;
    Dockspace    m_dockspace;
    IntroScreen  m_intro;   // with no project open, in its place

    ImageImporter       m_importer;
    Notices             m_notices;
    struct Drop { std::string path; ImVec2 pos; };
    std::vector<Drop>   m_drops;

    const InputFrame*   m_input = nullptr;
    const HostInfo*     m_host  = nullptr;

    // "Save changes to <name> before closing / quitting?" Yes: saved (asked where, if it has no
    // file yet), then closed or on to the next; No: closed unsaved, or left as it is; Cancel: stop.
    struct Confirm { bool quitting = false; ProjectId project = kNoProject; };
    std::optional<Confirm> m_confirm;   // asked now
    std::optional<Confirm> m_saving;    // answered Yes: waits for its save
    bool m_quitting = false;            // asking about each unsaved one in turn, then quitting
    std::vector<ProjectId> m_keptUnsaved;   // ... answered No so far

    static constexpr Clock::Milliseconds kHistoryShare{ 2.0 };   // a frame's time for that, at most

    bool m_initialized = false;
    bool m_quit = false;
    bool m_toldOutOfMemory = false;   // the user knows the GPU is full
};
