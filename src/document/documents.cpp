#include "document/documents.hpp"

#include <algorithm>

Documents::Documents()
{
    create();
}

Document* Documents::find(DocumentId id)
{
    for (const auto& d : m_open)
        if (d->id() == id) return d.get();
    return nullptr;
}

const Document* Documents::find(DocumentId id) const
{
    return const_cast<Documents*>(this)->find(id);
}

Document& Documents::create()
{
    if (!m_open.empty() && active().blank()) return active();
    auto doc = std::make_unique<Document>("Untitled " + std::to_string(++m_untitled));
    doc->workspace().init();
    return place(std::move(doc));
}

Document& Documents::open(std::unique_ptr<Document> doc)
{
    return place(std::move(doc));
}

Document& Documents::place(std::unique_ptr<Document> doc)
{
    doc->m_id = DocumentId(++m_lastId);
    const DocumentId id = doc->id();
    if (!m_open.empty() && active().blank()) {
        changed.emit({ DocumentsEvent::Type::Closing, active().id() });
        m_open[m_active] = std::move(doc);
    } else {
        m_active = m_open.empty() ? 0 : m_active + 1;
        m_open.insert(m_open.begin() + std::ptrdiff_t(m_active), std::move(doc));
    }
    changed.emit({ DocumentsEvent::Type::Opened, id });
    changed.emit({ DocumentsEvent::Type::Activated, id });
    return active();
}

bool Documents::activate(DocumentId id)
{
    const auto it = std::find_if(m_open.begin(), m_open.end(), [&](const auto& d) { return d->id() == id; });
    if (it == m_open.end()) return false;
    const size_t at = size_t(it - m_open.begin());
    if (at == m_active) return true;
    m_active = at;
    changed.emit({ DocumentsEvent::Type::Activated, id });
    return true;
}

bool Documents::close(DocumentId id)
{
    const auto it = std::find_if(m_open.begin(), m_open.end(), [&](const auto& d) { return d->id() == id; });
    if (it == m_open.end()) return false;
    const size_t at = size_t(it - m_open.begin());
    const bool wasActive = at == m_active;
    changed.emit({ DocumentsEvent::Type::Closing, id });   // still there: let go of it
    m_open.erase(m_open.begin() + std::ptrdiff_t(at));
    if (m_open.empty()) {
        m_active = 0;
        create();
        return true;
    }
    if (at < m_active || m_active == m_open.size()) --m_active;   // the one before it moved down; or it was last
    if (wasActive) changed.emit({ DocumentsEvent::Type::Activated, active().id() });
    return true;
}

bool Documents::anyUnsaved() const
{
    return std::any_of(m_open.begin(), m_open.end(), [](const auto& d) { return d->unsaved(); });
}
