#include "ui/unsaved-changes.hpp"
#include "project/project-actions.hpp"
#include "project/projects-manager-actions.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // DimBgRatio
#include <algorithm>
#include <cstdio>

void UnsavedChanges::init(const ProjectsManager& projects, ActionRegistry& actions, const InputFrame& input)
{
    m_projects = &projects;
    m_actions  = &actions;
    m_input    = &input;
}

void UnsavedChanges::askToClose(ProjectId project)
{
    if (m_confirm || m_saving) return;
    m_confirm = Confirm{ .quitting = false, .project = project };
}

void UnsavedChanges::askToQuit()
{
    if (m_quitting || m_saving) return;   // asking already
    m_quitting = true;
    m_keptUnsaved.clear();
    askNextBeforeQuit();
}

void UnsavedChanges::saved(ProjectId project, bool ok)
{
    if (m_saving && m_saving->project == project) answered(ok);
}

void UnsavedChanges::closing(ProjectId project)
{
    if (m_saving && m_saving->project == project) m_saving.reset();
    if (m_confirm && m_confirm->project == project) {   // gone without an answer: on to the next
        m_confirm.reset();
        if (m_quitting) askNextBeforeQuit();
    }
}

void UnsavedChanges::askNextBeforeQuit()
{
    for (const auto& d : m_projects->all())
        if (d->unsaved() && std::find(m_keptUnsaved.begin(), m_keptUnsaved.end(), d->id()) == m_keptUnsaved.end()) {
            m_confirm = Confirm{ .quitting = true, .project = d->id() };
            return;
        }
    m_quitting = false;
    m_quit = true;
}

void UnsavedChanges::answered(bool saved)
{
    const Confirm was = *m_saving;
    m_saving.reset();
    if (!saved) stop();   // the error is told (a notice), or the dialog was cancelled
    else if (was.quitting) askNextBeforeQuit();
    else m_actions->run(Actions::ProjectClose, { { ProjectArgs::kProject, was.project } });
}

void UnsavedChanges::stop()
{
    m_confirm.reset();
    m_saving.reset();
    m_quitting = false;
    m_keptUnsaved.clear();
}

void UnsavedChanges::draw()
{
    constexpr const char* kPopup = "Unsaved changes##confirm";
    if (!m_confirm) return;
    const Project* d = m_projects->find(m_confirm->project);
    if (!d) {
        stop();
        return;
    }
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));   // the window's middle, as its text changes too
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::GetCurrentContext()->DimBgRatio = 1.0f;   // the dim behind it at once (ImGui fades it in over a sixth of a second)
    char text[256];
    std::snprintf(text, sizeof text, "Save changes to %s before %s?", d->name().c_str(), m_confirm->quitting ? "quitting" : "closing");
    ImGui::TextUnformatted(text);
    Widgets::sectionGap();

    int answer = Widgets::buttonRow({ "Yes", "No", "Cancel" });   // alike, centred under the text
    if (m_actions->pressed(Actions::DialogCancel, *m_input)) answer = 2;

    const Confirm confirm = *m_confirm;
    switch (answer) {
    case 0:   // saved first (asked where, if it has no file yet): saved() goes on once it is
        m_confirm.reset();
        m_saving = confirm;
        if (!m_actions->run(Actions::FileSave, { { ProjectArgs::kProject, confirm.project } })) answered(false);   // can't now (an edit going on)
        break;
    case 1:
        m_confirm.reset();
        if (!confirm.quitting) {
            m_actions->run(Actions::ProjectClose, { { ProjectArgs::kProject, confirm.project }, { ProjectArgs::kDiscard, true } });
        } else {
            m_keptUnsaved.push_back(confirm.project);
            askNextBeforeQuit();   // the next one's question in this one's place
        }
        break;
    case 2:
        stop();
        break;
    default: break;
    }
    if (!m_confirm) {
        ImGui::CloseCurrentPopup();
        ImGui::GetCurrentContext()->DimBgRatio = 0.0f;   // ... and gone at once
    }
    ImGui::EndPopup();
}
