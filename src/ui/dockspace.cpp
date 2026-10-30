#include "ui/dockspace.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*, ImGuiDockNode
#include <algorithm>
#include <cmath>

namespace {

using DockShares = absl::flat_hash_map<unsigned int, float>;
using WindowIds  = std::vector<ImGuiID>;

// A node's size along `axis`: measured, or before the first layout (just loaded from the .ini)
// the size it was saved with.
float sizeOf(const ImGuiDockNode* n, int axis) { return n->Size[axis] > 0.0f ? n->Size[axis] : n->SizeRef[axis]; }

// Each split's first-child share, remembered (scaling from last frame's sizes would creep from
// rounding). Only a splitter the user moved (off by over a pixel) updates it.
float childShare(const ImGuiDockNode* c0, const ImGuiDockNode* c1, int axis)
{
    return sizeOf(c0, axis) / (sizeOf(c0, axis) + sizeOf(c1, axis));
}

void rememberShares(const ImGuiDockNode* node, DockShares& shares)
{
    if (node->IsLeafNode()) return;
    const ImGuiDockNode* c0 = node->ChildNodes[0];
    const ImGuiDockNode* c1 = node->ChildNodes[1];
    const int axis = node->SplitAxis;
    if (c0->IsVisible && c1->IsVisible && c0->Size[axis] > 0.0f && c1->Size[axis] > 0.0f) {
        const float total = c0->Size[axis] + c1->Size[axis];
        auto [it, inserted] = shares.try_emplace(node->ID, childShare(c0, c1, axis));
        if (!inserted && std::fabs(it->second * total - c0->Size[axis]) > 1.0f) it->second = childShare(c0, c1, axis);
    }
    rememberShares(c0, shares);
    rememberShares(c1, shares);
}

// Whether a dock area grows with the window: it holds a panel that does (the canvas, the graph).
// Others keep their size.
bool flexible(const ImGuiDockNode* node, const WindowIds& growing)
{
    if (!node) return false;
    if (!node->IsLeafNode()) return flexible(node->ChildNodes[0], growing) || flexible(node->ChildNodes[1], growing);
    for (const ImGuiWindow* w : node->Windows)
        if (std::find(growing.begin(), growing.end(), w->ID) != growing.end()) return true;
    return false;
}

// Re-lays the dock tree under `node` for `size` (ImGui alone gives the whole difference to one
// side). At each split:
//   flexible | fixed     the fixed side keeps its size (the side column keeps its width)
//   otherwise            each keeps its share (stacked panels grow together)
// Writes the children's SizeRef, which the dockspace uses this frame.
void scaleDockTree(ImGuiDockNode* node, ImVec2 size, const DockShares& shares, const WindowIds& growing)
{
    if (node->IsLeafNode()) return;
    ImGuiDockNode* c0 = node->ChildNodes[0];
    ImGuiDockNode* c1 = node->ChildNodes[1];
    const int axis = node->SplitAxis, across = axis ^ 1;
    ImVec2 s0 = size, s1 = size;
    if (c0->IsVisible && c1->IsVisible && sizeOf(c0, axis) > 0.0f && sizeOf(c1, axis) > 0.0f) {
        const float avail = std::max(size[axis] - ImGui::GetStyle().DockingSeparatorSize, 2.0f);
        const bool flex0 = flexible(c0, growing), flex1 = flexible(c1, growing);
        if (flex0 == flex1) {
            const auto it = shares.find(node->ID);
            const float share = it != shares.end() ? it->second : childShare(c0, c1, axis);
            s0[axis] = std::max(1.0f, std::round(share * avail));
            s1[axis] = std::max(1.0f, avail - s0[axis]);
        } else {
            ImGuiDockNode* kept = flex0 ? c1 : c0;   // the fixed side keeps its size
            const float k = std::clamp(sizeOf(kept, axis), 1.0f, avail - 1.0f);
            (kept == c0 ? s0 : s1)[axis] = k;
            (kept == c0 ? s1 : s0)[axis] = avail - k;
        }
        c0->SizeRef[axis] = s0[axis];
        c1->SizeRef[axis] = s1[axis];
    }
    s0[across] = s1[across] = size[across];
    scaleDockTree(c0, s0, shares, growing);
    scaleDockTree(c1, s1, shares, growing);
}

}

void Dockspace::addPanel(const PanelInfo& panel)
{
    m_panels.push_back(panel);
    if (panel.slot == DockSlot::Center || panel.slot == DockSlot::Bottom) m_growing.push_back(ImHashStr(panel.title));   // a window's id: its title, hashed
}

void Dockspace::draw()
{
    static const ImGuiID kDockId = ImHashStr("KomalMainDockspace.v7");   // bumped when the default layout changes
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (m_reset || ImGui::DockBuilderGetNode(kDockId) == nullptr) {
        buildDefault(kDockId, vp->WorkSize.x, vp->WorkSize.y);
        m_reset = false;
        m_shares.clear();
    }
    // The window resized: the layout scales with it. Otherwise the shares follow the splitters.
    if (ImGuiDockNode* root = ImGui::DockBuilderGetNode(kDockId); root && root->Size.x > 0.0f && root->Size.y > 0.0f) {
        if (root->Size.x != vp->WorkSize.x || root->Size.y != vp->WorkSize.y) scaleDockTree(root, vp->WorkSize, m_shares, m_growing);
        else                                                                   rememberShares(root, m_shares);
    }
    ImGui::DockSpaceOverViewport(kDockId, vp);
}

void Dockspace::buildDefault(unsigned int id, float w, float h)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImVec2(w, h));

    // Center over Bottom | the side column: RightTop over RightMiddle over RightLower over
    // RightBottom. Panels sharing a slot are tabs, in the order they were added; the toolbar is
    // above, floating panels float.
    ImGuiID center = id;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    const ImGuiID top    = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.32f, nullptr, &right);
    const ImGuiID middle = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.48f, nullptr, &right);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);
    const ImGuiID under  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.48f, nullptr, &center);

    for (const PanelInfo& p : m_panels) {
        ImGuiID node = 0;
        switch (p.slot) {
        case DockSlot::Center:      node = center; break;
        case DockSlot::Bottom:      node = under;  break;
        case DockSlot::RightTop:    node = top;    break;
        case DockSlot::RightMiddle: node = middle; break;
        case DockSlot::RightLower:  node = right;  break;
        case DockSlot::RightBottom: node = bottom; break;
        case DockSlot::Toolbar:
        case DockSlot::Floating:    continue;
        }
        ImGui::DockBuilderDockWindow(p.title, node);
    }
    ImGui::DockBuilderFinish(id);
}
