#include "document/document.hpp"
#include "base/file.hpp"
#include "document/workspace-file.hpp"

#include <cctype>
#include <string_view>

void Document::savedAs(const std::string& path)
{
    m_path = path;
    m_name = File::stem(path);
    m_savedStep = m_workspace.history().current();
}

bool Document::isFileName(const std::string& path)
{
    const std::string_view ext = kExtension;
    if (path.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(path[path.size() - ext.size() + i])) != ext[i]) return false;
    return true;
}

bool Document::save(std::string path, std::string& error)
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

std::unique_ptr<Document> Document::open(const std::string& path, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!File::read(path, bytes, error)) return nullptr;
    const std::optional<WorkspaceFile> file = decodeWorkspace(bytes, error);
    if (!file) return nullptr;
    auto doc = std::make_unique<Document>(File::stem(path));
    if (!doc->workspace().open(*file, error)) return nullptr;
    doc->savedAs(path);
    return doc;
}
