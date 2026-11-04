#include "ui/panels/graph/graph-menus.hpp"
#include "ui/style/ui-style.hpp"
#include "document/document-actions.hpp"
#include "document/workspace.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <cmath>
#include <cstdio>

using namespace GraphLayout;
using namespace GraphStyle;
using namespace DocArgs;

namespace {

constexpr const char* kContext    = "##node-context";
constexpr const char* kArtboardMenu = "##artboard-context";
constexpr const char* kColorPopup = "##artboard-color";
constexpr const char* kValuePopup = "##value-edit";

}

void GraphMenus::init(const Workspace& workspace, ActionRegistry& actions)
{
    m_workspace = &workspace;
    m_actions = &actions;
}

void GraphMenus::openAdd(Wiring wiring, ImVec2 graphPos)
{
    const NodeMenu::Fit fit = std::holds_alternative<ToInput>(wiring) ? NodeMenu::Fit::NeedsOutput
                            : std::holds_alternative<Free>(wiring)    ? NodeMenu::Fit::Any
                                                                       : NodeMenu::Fit::NeedsInput;
    m_wiring = std::move(wiring);
    m_addAt  = graphPos;
    m_addMenu.open(fit);
}

void GraphMenus::openContext(NodeId node)
{
    m_contextNode = node;
    ImGui::OpenPopup(kContext);   // in the graph's window, like the popup itself
}

void GraphMenus::openArtboard(ArtboardId id)
{
    m_artboard = id;
    ImGui::OpenPopup(kArtboardMenu);
}

void GraphMenus::openColor(ArtboardId id)
{
    const Artboard* a = m_workspace->artboard(id);
    if (!a) return;
    m_artboard = id;
    m_colorPick.open(a->color);
}

bool GraphMenus::openValue(Value value)
{
    // Starts from what the node (or the artboard) has now.
    const bool known = std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RenameArtboard>) {
            const Artboard* a = m_workspace->artboard(v.artboard);
            if (a) std::snprintf(v.text, sizeof v.text, "%s", a->name.c_str());
            return a != nullptr;
        }
        else {
        const Node* n = m_workspace->find(v.node);
        if (!n) return false;
        if constexpr (std::is_same_v<T, Rename>) {
            std::snprintf(v.text, sizeof v.text, "%s", n->label().c_str());
        } else if constexpr (std::is_same_v<T, ScaleValue>) {
            v.percent = std::round(n->signal(v.signal).scale[int(v.channel)] * 1000.0f) / 10.0f;
        } else {
            const NodeSettings settings = n->settings();
            const auto* move = std::get_if<MoveSettings>(&settings);
            if (!move) return false;
            v.pixels = float(v.axis == 0 ? move->x : move->y);
        }
        return true;
        }
    }, value);
    if (!known) return false;
    m_value = value;
    m_valuePending = true;
    m_valueJustOpened = true;
    return true;
}

void GraphMenus::draw()
{
    NodeType picked;
    if (m_addMenu.draw(picked)) add(picked);
    drawContext();
    drawArtboard();
    drawColor();
    drawValue();
}

void GraphMenus::add(NodeType type)
{
    // Placed so what it's wired by sits where the wiring was asked for.
    const float portY = kHeaderH + kIoRowH * 0.5f;   // where a node's I/O row sits, below its top
    float x = m_addAt.x, y = m_addAt.y;
    ActionArgs wired;
    std::visit([&](const auto& w) {
        using T = std::decay_t<decltype(w)>;
        if constexpr (std::is_same_v<T, FromOutput>) { y -= portY; wired.add({ kFrom, w.from }); }
        if constexpr (std::is_same_v<T, ToInput>)    { x -= kNodeWidth; y -= portY; wired.add({ kInto, w.to }); wired.add({ kSlot, w.slot }); }
        if constexpr (std::is_same_v<T, Insert>) {
            x -= kNodeWidth * 0.5f;
            y -= portY;
            wired.add({ kFrom, w.from });
            wired.add({ kInto, w.to });
            wired.add({ kSlot, w.slot });
        }
    }, m_wiring);
    ActionArgs args{ { kType, nodeKind(type).id }, { kX, x }, { kY, y } };
    args.append(wired);
    const ActionResult r = m_actions->run(Actions::NodeAdd, std::move(args));
    if (!r) return;
    m_actions->run(Actions::SelectionSet, selectionArgs(NodeSelection::only(NodeId(r.made))));   // in front already: it's the newest
}

void GraphMenus::drawContext()
{
    if (!ImGui::BeginPopup(kContext)) return;
    const Node* n = m_workspace->find(m_contextNode);
    if (!n) {
        ImGui::CloseCurrentPopup();
    } else {
        // Every entry is an action, run on this node (or the selection it's in).
        using Widgets::actionMenuItem;
        ActionRegistry& actions = *m_actions;
        const NodeId id = n->id();
        const bool master = m_workspace->isMaster(id);
        const NodeSettings settings = n->settings();
        ImGui::TextDisabled("%s", n->label().c_str());
        ImGui::Separator();
        if (const auto* raster = std::get_if<RasterSettings>(&settings)) {
            const bool active = m_workspace->activeRasterId() == id;
            actionMenuItem(actions, Actions::RasterActivate, { { kNode, id } }, { .checked = active, .enabled = !active });
            actionMenuItem(actions, Actions::RasterLock, { { kNode, id }, { kOn, !raster->locked } }, { .checked = raster->locked });
            actionMenuItem(actions, Actions::RasterLockAlpha, { { kNode, id }, { kOn, !raster->alphaLocked } }, { .checked = raster->alphaLocked });
        }
        if (!master) {
            const NodeId out = m_workspace->master().id();
            const InputSlot top = InputSlot::newLayer(uint32_t(m_workspace->master().layers().size()));
            actionMenuItem(actions, Actions::LinkConnect, { { kFrom, id }, { kTo, out }, { kSlot, top } },
                           { .label = "Add to Master", .enabled = m_workspace->canConnect(id, out, top) });
            if (const ActionResult ref = actionMenuItem(actions, Actions::NodeReference, { { kNode, id }, { kX, n->x() + 30.0f }, { kY, n->y() + 30.0f } },
                                                        { .label = "Reference it" }))
                actions.run(Actions::SelectionSet, selectionArgs(NodeSelection::only(NodeId(ref.made))));
        }
        // A right click selects the node unless it's already in the selection: then it's all of it.
        const NodeSelection& selection = m_workspace->selection();
        const bool many = selection.contains(id) && selection.size() > 1;
        const std::vector<NodeId> shown = many ? selection.list() : std::vector<NodeId>{ id };
        actionMenuItem(actions, Actions::GraphShowInCanvas, { { kNodes, shown } }, { .label = many ? "Show selected in Canvas" : "Show in Canvas" });
        if (const auto* move = std::get_if<MoveSettings>(&settings)) {
            // Lost far away: shift it so what it outputs starts at the canvas origin.
            const BoundsI b = n->bounds();
            if (actionMenuItem(actions, Actions::MoveSet, { { kNode, id }, { kOffsetX, move->x - b.minX }, { kOffsetY, move->y - b.minY } },
                               { .label = "Move to Origin", .enabled = !b.empty() && (b.minX != 0 || b.minY != 0) }))
                actions.run(Actions::GraphShowInCanvas, { { kNodes, std::vector<NodeId>{ id } } });
        }
        if (master) {
            // A new one over what the canvas shows (or a 1920 x 1080 from the origin while it's hidden).
            const Vec2 view = m_workspace->canvasViewport();
            BoundsI rect{ 0, 0, 1919, 1079 };
            if (view.x > 0.0f && view.y > 0.0f) {
                const ViewRect v = m_workspace->canvasCamera().visibleWorld(view.x, view.y);
                rect = { int(std::floor(v.minX)), int(std::floor(v.minY)), int(std::ceil(v.maxX)) - 1, int(std::ceil(v.maxY)) - 1 };
            }
            actionMenuItem(actions, Actions::ArtboardAdd, { { kMinX, rect.minX }, { kMinY, rect.minY }, { kMaxX, rect.maxX }, { kMaxY, rect.maxY } });
        }
        ImGui::Separator();
        actionMenuItem(actions, Actions::GraphRename, { { kNode, id } });
        if (!master) {
            ImGui::Separator();
            actionMenuItem(actions, Actions::GraphDelete, {}, { .label = many ? "Delete selected" : "Delete" });
        }
    }
    ImGui::EndPopup();
}

void GraphMenus::drawArtboard()
{
    if (!ImGui::BeginPopup(kArtboardMenu)) return;
    if (const Artboard* a = m_workspace->artboard(m_artboard)) {
        using Widgets::actionMenuItem;
        ImGui::TextDisabled("%s", a->name.c_str());
        ImGui::Separator();
        actionMenuItem(*m_actions, Actions::ArtboardSet, { { kArtboard, a->id }, { kVisible, !a->visible } },
                       { .label = "Shown", .checked = a->visible });
        if (ImGui::MenuItem("Color...")) openColor(a->id);
        if (ImGui::MenuItem("Rename")) openValue(RenameArtboard{ a->id });
        ImGui::Separator();
        actionMenuItem(*m_actions, Actions::ArtboardDelete, { { kArtboard, a->id } });
    } else {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void GraphMenus::drawColor()
{
    // Shown as it's picked; the artboard takes it once a change is finished (one undo step each).
    const FloatColorPick::Result r = m_colorPick.draw(kColorPopup);
    const Artboard* a = m_workspace->artboard(m_artboard);
    if (r.done && a && r.color != a->color) m_actions->run(Actions::ArtboardSet, { { kArtboard, a->id }, { kColor, r.color } });
}

void GraphMenus::drawValue()
{
    if (std::exchange(m_valuePending, false)) ImGui::OpenPopup(kValuePopup);
    if (!ImGui::BeginPopup(kValuePopup)) return;
    // One field, typing from the start; the popup closes once it's left (applied or not).
    static constexpr const char* kField = "##value";
    if (m_valueJustOpened) Widgets::focusField(kField);
    const float width = Style::get().menu.valueWidth;
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Rename>) {
            if (Widgets::textField(kField, v.text, sizeof v.text, width))
                m_actions->run(Actions::NodeRename, { { kNode, v.node }, { kLabel, v.text } });
        } else if constexpr (std::is_same_v<T, RenameArtboard>) {
            if (Widgets::textField(kField, v.text, sizeof v.text, width) && v.text[0])
                m_actions->run(Actions::ArtboardSet, { { kArtboard, v.artboard }, { kName, std::string(v.text) } });
        } else if constexpr (std::is_same_v<T, ScaleValue>) {
            if (Widgets::numberField(kField, v.percent, 0.0f, 100.0f, "%.1f%%", width))
                m_actions->run(Actions::SignalSet, { { kNode, v.node }, { kSignal, v.signal }, { kChannel, v.channel }, { kValue, v.percent / 100.0f } });
        } else {
            const Node* n = m_workspace->find(v.node);
            const NodeSettings settings = n ? n->settings() : NodeSettings{};
            const auto* move = std::get_if<MoveSettings>(&settings);
            if (move && Widgets::numberField(kField, v.pixels, -1e9f, 1e9f, "%.0f px", width)) {
                const int p = int(std::lround(v.pixels));
                const int x = v.axis == 0 ? p : move->x, y = v.axis == 0 ? move->y : p;
                m_actions->run(Actions::MoveSet, { { kNode, v.node }, { kOffsetX, x }, { kOffsetY, y } });
            }
        }
    }, m_value);
    if (!m_valueJustOpened && !Widgets::isEditing(kField)) ImGui::CloseCurrentPopup();
    m_valueJustOpened = false;
    ImGui::EndPopup();
}
