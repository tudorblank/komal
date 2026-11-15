#include "project/project.hpp"
#include "base/file.hpp"
#include "project/workspace-file.hpp"

void Project::savedAs(const std::string& path)
{
    m_path = path;
    m_name = File::stem(path);
    m_savedStep = m_workspace.history().current();
}

bool Project::isFileName(const std::string& path)
{
    return File::hasExtension(path, kExtension);
}

bool Project::save(std::string path, std::string& error)
{
    if (path.empty()) {
        error = "no file to save to";
        return false;
    }
    if (!isFileName(path)) path += kExtension;
    if (!File::replace(path, encodeWorkspace(m_workspace.saved()), error)) return false;
    savedAs(path);
    return true;
}

std::unique_ptr<Project> Project::open(const std::string& path, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!File::read(path, bytes, error)) return nullptr;
    const std::optional<WorkspaceFile> file = decodeWorkspace(bytes, error);
    if (!file) return nullptr;
    auto project = std::make_unique<Project>(File::stem(path));
    if (!project->workspace().open(*file, error)) return nullptr;
    project->savedAs(path);
    return project;
}
