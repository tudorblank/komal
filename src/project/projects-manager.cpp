#include "project/projects-manager.hpp"

#include <algorithm>

std::optional<size_t> ProjectsManager::indexOf(ProjectId id) const
{
    for (size_t i = 0; i < m_open.size(); ++i)
        if (m_open[i]->id() == id) return i;
    return std::nullopt;
}

Project* ProjectsManager::find(ProjectId id)
{
    const std::optional<size_t> at = indexOf(id);
    return at ? m_open[*at].get() : nullptr;
}

const Project* ProjectsManager::find(ProjectId id) const
{
    return const_cast<ProjectsManager*>(this)->find(id);
}

Project& ProjectsManager::create()
{
    auto project = std::make_unique<Project>("Untitled " + std::to_string(++m_untitled));
    project->workspace().init();
    return add(std::move(project));
}

Project& ProjectsManager::add(std::unique_ptr<Project> project)
{
    project->m_id = ProjectId(++m_lastId);
    const ProjectId id = project->id();
    m_active = m_open.empty() ? 0 : m_active + 1;
    m_open.insert(m_open.begin() + std::ptrdiff_t(m_active), std::move(project));
    changed.emit({ ProjectsEvent::Type::Opened, id });
    changed.emit({ ProjectsEvent::Type::Activated, id });
    return active();
}

bool ProjectsManager::activate(ProjectId id)
{
    const std::optional<size_t> at = indexOf(id);
    if (!at) return false;
    if (*at == m_active) return true;
    m_active = *at;
    changed.emit({ ProjectsEvent::Type::Activated, id });
    return true;
}

bool ProjectsManager::close(ProjectId id)
{
    const std::optional<size_t> found = indexOf(id);
    if (!found) return false;
    const size_t at = *found;
    const bool wasActive = at == m_active;
    changed.emit({ ProjectsEvent::Type::Closing, id });   // still there: let go of it
    m_open.erase(m_open.begin() + std::ptrdiff_t(at));
    if (m_open.empty()) {   // none left
        m_active = 0;
        return true;
    }
    if (at < m_active || m_active == m_open.size()) --m_active;   // the one before it moved down; or it was last
    if (wasActive) changed.emit({ ProjectsEvent::Type::Activated, active().id() });
    return true;
}

bool ProjectsManager::anyUnsaved() const
{
    return std::any_of(m_open.begin(), m_open.end(), [](const auto& project) { return project->unsaved(); });
}
