#include "ui/graph-panel/graph-menus.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"
#include "graph/style.hpp"
#include "ui/widgets/widgets.hpp"

#include <cmath>
#include <cstdio>

using namespace GraphStyle;

namespace {

constexpr const char* kContext = "##node-context";
constexpr const char* kValue   = "##value-edit";

}

void GraphMenus::init(const Project& project, ActionRegistry& actions)
{
    m_project = &project;
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

bool GraphMenus::openValue(Value value)
{
    // Starts from what the node has now.
    const bool known = std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        const Node* n = m_project->find(v.node);
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
        if constexpr (std::is_same_v<T, FromOutput>) { y -= portY; wired.add({ "from", w.from }); }
        if constexpr (std::is_same_v<T, ToInput>)    { x -= kNodeWidth; y -= portY; wired.add({ "into", w.to }); wired.add({ "slot", w.slot.text() }); }
        if constexpr (std::is_same_v<T, Insert>) {
            x -= kNodeWidth * 0.5f;
            y -= portY;
            wired.add({ "from", w.from });
            wired.add({ "into", w.to });
            wired.add({ "slot", w.slot.text() });
        }
    }, m_wiring);
    ActionArgs args{ { "type", nodeKind(type).id }, { "x", x }, { "y", y } };
    args.append(wired);
    const ActionResult r = m_actions->run(Actions::NodeAdd, std::move(args));
    if (!r) return;
    m_actions->run(Actions::SelectionSet, selectionArgs(NodeSelection::only(NodeId(r.made))));   // in front already: it's the newest
}

void GraphMenus::drawContext()
{
    if (!ImGui::BeginPopup(kContext)) return;
    const Node* n = m_project->find(m_contextNode);
    if (!n) {
        ImGui::CloseCurrentPopup();
    } else {
        // Every entry is an action, run on this node (or the selection it's in).
        using Widgets::actionMenuItem;
        ActionRegistry& actions = *m_actions;
        const NodeId id = n->id();
        const bool master = m_project->isMaster(id);
        const NodeSettings settings = n->settings();
        ImGui::TextDisabled("%s", n->label().c_str());
        ImGui::Separator();
        if (const auto* raster = std::get_if<RasterSettings>(&settings)) {
            const bool active = m_project->activeRasterId() == id;
            actionMenuItem(actions, Actions::RasterActivate, { { "node", id } }, { .checked = active, .enabled = !active });
            actionMenuItem(actions, Actions::RasterLock, { { "node", id }, { "on", !raster->locked } }, { .checked = raster->locked });
            actionMenuItem(actions, Actions::RasterLockAlpha, { { "node", id }, { "on", !raster->alphaLocked } }, { .checked = raster->alphaLocked });
        }
        if (!master) {
            const NodeId out = m_project->master().id();
            const InputSlot top = InputSlot::newLayer(uint32_t(m_project->master().layers().size()));
            actionMenuItem(actions, Actions::LinkConnect, { { "from", id }, { "to", out }, { "slot", top.text() } },
                           { .label = "Add to Master", .enabled = m_project->canConnect(id, out, top) });
            if (const ActionResult ref = actionMenuItem(actions, Actions::NodeReference, { { "node", id }, { "x", n->x() + 30.0f }, { "y", n->y() + 30.0f } },
                                                        { .label = "Reference it" }))
                actions.run(Actions::SelectionSet, selectionArgs(NodeSelection::only(NodeId(ref.made))));
        }
        // A right click selects the node unless it's already in the selection: then it's all of it.
        const NodeSelection& selection = m_project->selection();
        const bool many = selection.contains(id) && selection.size() > 1;
        const std::vector<NodeId> shown = many ? selection.list() : std::vector<NodeId>{ id };
        actionMenuItem(actions, Actions::GraphShowInCanvas, { { "nodes", shown } }, { .label = many ? "Show selected in Canvas" : "Show in Canvas" });
        if (const auto* move = std::get_if<MoveSettings>(&settings)) {
            // Lost far away: shift it so what it outputs starts at the canvas origin.
            const BoundsI b = n->bounds();
            if (actionMenuItem(actions, Actions::MoveSet, { { "node", id }, { "x", move->x - b.minX }, { "y", move->y - b.minY } },
                               { .label = "Move to Origin", .enabled = !b.empty() && (b.minX != 0 || b.minY != 0) }))
                actions.run(Actions::GraphShowInCanvas, { { "nodes", std::vector<NodeId>{ id } } });
        }
        ImGui::Separator();
        actionMenuItem(actions, Actions::GraphRename, { { "node", id } });
        if (!master) {
            ImGui::Separator();
            actionMenuItem(actions, Actions::GraphDelete, {}, { .label = many ? "Delete selected" : "Delete" });
        }
    }
    ImGui::EndPopup();
}

void GraphMenus::drawValue()
{
    if (std::exchange(m_valuePending, false)) ImGui::OpenPopup(kValue);
    if (!ImGui::BeginPopup(kValue)) return;
    // One field, typing from the start; the popup closes once it's left (applied or not).
    static constexpr const char* kField = "##value";
    if (m_valueJustOpened) Widgets::focusField(kField);
    const float width = ImGui::GetFontSize() * 10.0f;
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Rename>) {
            if (Widgets::textField(kField, v.text, sizeof v.text, width))
                m_actions->run(Actions::NodeRename, { { "node", v.node }, { "label", v.text } });
        } else if constexpr (std::is_same_v<T, ScaleValue>) {
            if (Widgets::numberField(kField, v.percent, 0.0f, 100.0f, "%.1f%%", width))
                m_actions->run(Actions::SignalSet, { { "node", v.node }, { "signal", v.signal }, { "channel", v.channel }, { "value", v.percent / 100.0f } });
        } else {
            const Node* n = m_project->find(v.node);
            const NodeSettings settings = n ? n->settings() : NodeSettings{};
            const auto* move = std::get_if<MoveSettings>(&settings);
            if (move && Widgets::numberField(kField, v.pixels, -1e9f, 1e9f, "%.0f px", width)) {
                const int p = int(std::lround(v.pixels));
                m_actions->run(Actions::MoveSet, { { "node", v.node }, { "x", v.axis == 0 ? p : move->x }, { "y", v.axis == 0 ? move->y : p } });
            }
        }
    }, m_value);
    if (!m_valueJustOpened && !Widgets::isEditing(kField)) ImGui::CloseCurrentPopup();
    m_valueJustOpened = false;
    ImGui::EndPopup();
}
