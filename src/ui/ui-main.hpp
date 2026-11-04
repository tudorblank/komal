#pragma once
// The app's interface: the menu bar, the dock layout and every panel, and where a dropped file
// goes (the panel under it). Panels never reach each other: they share the active document's
// workspace. Panels are one list (ui/panels/panel-base.hpp): their View toggles, close buttons,
// default layout, draw order, GPU work and frames all come from it.
//
// It's a shell over the open documents (Documents): it holds the app's own actions (the app's,
// the panels', the tools', the colours': none of them a document's) and hands the rest to the
// active document's registry. Switching documents switches everything to that one: the canvas
// (one per document), the graph, the navigator, the history. The colours and the tools stay:
// they're the app's. Closing an unsaved document, or quitting with one, asks first. With none
// open (at the start, after the last one closed) the bars stay and the intro takes the dock
// area: no panel of it draws.
//
// The open documents change between frames, never while one is drawn: what a frame drew with (a
// canvas's picture, a thumbnail) must last until it's shown. The doc.* actions the interface runs
// ask for the change; the documents' own actions make it at the start of the next frame.
//
// The ui folder, by job:
//
//   style/     the look, defined once: theme (reads a theme file, assets/themes/), palette (every
//              colour by its job, made from the theme: the one source), fonts, ui-style (ImGui's
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
#include "document/documents.hpp"
#include "ui/dockspace.hpp"
#include "ui/panels/intro-panel.hpp"
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

    // After ImGui + wgpu are up. It follows `documents` from then on (they outlive it).
    bool init(GpuDevice& gpu, Documents& documents, const InputFrame& input, const HostInfo& host);
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
    // Any thread (a dialog's, the command line): a .komal file opens as a document, anything else
    // is imported as an image.
    void openFile(std::string path);
    void saveTo(DocumentId document, std::string path);   // any thread: the save dialog's answer (empty: cancelled)
    // A file dropped at window (x, y): a .komal file opens wherever it lands; an image is imported
    // if it lands on the canvas or the graph.
    void dropFile(std::string path, float x, float y) { m_drops.push_back({ std::move(path), ImVec2(x, y) }); }

private:
    struct PanelEntry {
        std::unique_ptr<Panel> panel;
        PanelInfo              info;   // as it said when added
    };

    template<class T> T* addPanel();
    // The bars always; the dock area's panels only with a document open (the intro's in their place).
    bool drawn(const PanelEntry& e) const;
    void drawMenuBar();
    void registerAppActions();   // ui/app-actions.cpp: every app-state action
    void dispatchDrops();
    void documentsChanged(const DocumentsEvent& e);   // the engine and every panel follow
    void registerDocumentRequests();   // doc.* here: each asks for its change, made between frames
    void applyDocumentRequests();      // ... the changes asked for, before anything is drawn
    void takeInbox();                  // the files that came from other threads, as document requests
    void settleHistories();            // once a frame, last: history's pixels warmed and cooled (Workspace::settleHistory)
    // ---- unsaved changes: asked first, one document at a time (closing it, or quitting with it)
    void ask(DocumentId document, bool quitting);
    void askNextBeforeQuit();       // the next unsaved one not answered yet; none left: quit
    void answered(bool saved);      // its "Yes" saved it (or didn't): on to what was waiting
    void stopAsking();              // Cancel: nothing closes, no quitting
    void drawConfirm();

    ActionRegistry m_actions;   // the app's own; the active document's through it (delegateTo)
    Documents*     m_documents = nullptr;
    Connection     m_documentsEvents;
    ActionRegistry m_documentActions;   // the documents' own (document/documents-actions): logged as they run
    std::vector<std::pair<ActionId, ActionArgs>> m_documentRequests;   // asked for this frame, in order
    // Files that came from another thread (a dialog, the command line), for the next frame.
    std::mutex m_inboxLock;
    std::vector<std::string> m_toOpen;
    std::vector<std::pair<DocumentId, std::string>> m_toSave;
    ToolManager    m_tools;     // before the panels that use it
    InputOwner     m_owner;     // who has the keyboard this frame
    CanvasEngine   m_canvas;    // ... and what canvas views draw with
    std::optional<UiContext> m_context;   // what every panel reads (Panel::ui()), from init() on

    std::vector<PanelEntry>  m_panels;      // in View menu order, also their GPU work's (the canvas first)
    std::vector<PanelEntry*> m_drawOrder;   // Edit panels, then Show panels (PanelInfo::phase), each in menu order
    CanvasPanel* m_canvasPanel = nullptr;   // in m_panels: the ones a drop can land on
    GraphPanel*  m_graphPanel  = nullptr;
    Dockspace    m_dockspace;
    IntroPanel   m_intro;   // with no document open, in its place

    ImageImporter       m_importer;
    Notices             m_notices;
    struct Drop { std::string path; ImVec2 pos; };
    std::vector<Drop>   m_drops;

    const InputFrame*   m_input = nullptr;
    const HostInfo*     m_host  = nullptr;

    // "Save changes to <name> before closing / quitting?" Yes: saved (asked where, if it has no
    // file yet), then closed or on to the next; No: closed unsaved, or left as it is; Cancel: stop.
    struct Confirm { bool quitting = false; DocumentId document = kNoDocument; };
    std::optional<Confirm> m_confirm;   // asked now
    std::optional<Confirm> m_saving;    // answered Yes: waits for its save
    bool m_quitting = false;            // asking about each unsaved one in turn, then quitting
    std::vector<DocumentId> m_keptUnsaved;   // ... answered No so far

    static constexpr Clock::Milliseconds kHistoryShare{ 2.0 };   // a frame's time for that, at most

    bool m_initialized = false;
    bool m_quit = false;
    bool m_toldOutOfMemory = false;   // the user knows the GPU is full
};
