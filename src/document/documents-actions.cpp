#include "document/documents-actions.hpp"
#include "document/documents.hpp"

using namespace DocArgs;

void registerDocumentsActions(ActionRegistry& actions, Documents& d)
{
    using K = ActionKind;
    const auto settled = [&d] {   // nothing going on in the active one (or none open)
        if (d.empty()) return true;
        const Workspace& w = d.active().workspace();
        return !w.strokeActive() && !w.preview();
    };

    actions.add({ .id = Actions::DocNew, .title = "New", .kind = K::AppState, .enabled = settled,
                  .run = [&d](const ActionArgs&) -> ActionResult { return { true, int64_t(d.create().id()) }; } });
    actions.add({ .id = Actions::DocClose, .title = "Close", .kind = K::AppState,
                  .args = { Arg::id(kDocument).optional(), Arg::flag(kDiscard).optional() }, .enabled = settled, .run = [&d](const ActionArgs& a) {
        const Document* doc = d.find(a.get(kDocument, d.activeId()));
        if (!doc || (doc->unsaved() && !a.get(kDiscard, false))) return false;
        return d.close(doc->id());
    } });
    actions.add({ .id = Actions::DocSave, .title = "Save", .kind = K::AppState,
                  .args = { Arg::id(kDocument).optional(), Arg::text(kPath).optional() }, .enabled = settled,
                  .run = [&d](const ActionArgs& a) -> ActionResult {
        Document* doc = d.find(a.get(kDocument, d.activeId()));
        if (!doc) return ActionResult::refused("no such document");
        std::string why;
        if (!doc->save(a.get(kPath, doc->path()), why)) return ActionResult::refused(why);
        return { true, int64_t(doc->id()) };
    } });
    actions.add({ .id = Actions::DocOpen, .title = "Open", .kind = K::AppState, .args = { Arg::text(kPath) }, .enabled = settled,
                  .run = [&d](const ActionArgs& a) -> ActionResult {
        const std::string path = a.get(kPath);
        for (const auto& open : d.all())
            if (!open->path().empty() && open->path() == path) {
                d.activate(open->id());
                return { true, int64_t(open->id()) };
            }
        std::string why;
        std::unique_ptr<Document> doc = Document::open(path, why);
        if (!doc) return ActionResult::refused(why);
        return { true, int64_t(d.open(std::move(doc)).id()) };
    } });
    actions.add({ .id = Actions::DocActivate, .title = "Switch to", .kind = K::AppState,
                  .args = { Arg::id(kDocument) }, .enabled = settled, .run = [&d](const ActionArgs& a) { return d.activate(a.get(kDocument)); } });
}
