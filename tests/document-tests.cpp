// The documents open at once: each a workspace with its own actions and history, and its file.
// None open at first, none after the last one closes; a new one is always new. Unsaved: its
// history isn't at the step its file holds.

#include "helpers.hpp"
#include "document/documents.hpp"
#include "document/documents-actions.hpp"

#include <algorithm>

namespace {

// One edit, through the document's own actions: a step of its own history.
void edit(Document& d)
{
    REQUIRE(d.actions().run(Actions::NodeAdd, { { DocArgs::kType, "move" }, { DocArgs::kX, 0.0f }, { DocArgs::kY, 0.0f } }));
}

std::unique_ptr<Document> fromFile(const std::string& path)   // as the file format will hand one over
{
    auto d = std::make_unique<Document>();
    d->workspace().init();
    d->savedAs(path);
    return d;
}

}

TEST_CASE("a blank slate: never edited, no file; an edit makes it a project, unsaved")
{
    Document d;
    d.workspace().init();
    CHECK(d.blank());
    CHECK_FALSE(d.unsaved());
    edit(d);
    CHECK_FALSE(d.blank());
    CHECK(d.unsaved());
    REQUIRE(d.actions().run(Actions::EditUndo));   // back to how it began: nothing to lose ...
    CHECK_FALSE(d.unsaved());
    CHECK_FALSE(d.blank());                        // ... but it was edited
}

TEST_CASE("unsaved: away from the step its file holds, whichever way")
{
    Document d;
    d.workspace().init();
    edit(d);
    d.savedAs("/somewhere/poster.komal");
    CHECK(d.name() == "poster");
    CHECK(d.path() == "/somewhere/poster.komal");
    CHECK_FALSE(d.unsaved());
    CHECK_FALSE(d.blank());
    REQUIRE(d.actions().run(Actions::EditUndo));   // before it
    CHECK(d.unsaved());
    REQUIRE(d.actions().run(Actions::EditRedo));   // back on it
    CHECK_FALSE(d.unsaved());
    REQUIRE(d.actions().run(Actions::EditUndo));
    edit(d);                                       // another branch
    CHECK(d.unsaved());
}

TEST_CASE("each document has its own workspace, history and actions")
{
    Documents docs;
    Document& one = docs.create();
    edit(one);
    Document& two = docs.create();
    REQUIRE(&two != &one);
    CHECK(two.blank());
    CHECK(two.workspace().history().size() == 1);
    CHECK_FALSE(two.actions().run(Actions::EditUndo).done);   // nothing of its own to undo
    CHECK(one.workspace().history().size() == 2);
    const auto logged = [](const Document& d, ActionId id) {
        const auto& log = d.actions().log();
        return std::any_of(log.begin(), log.end(), [&](const ActionLogEntry& e) { return e.id == id; });
    };
    CHECK(logged(one, Actions::NodeAdd));         // each its own log
    CHECK_FALSE(logged(two, Actions::NodeAdd));
}

TEST_CASE("none open at first; new ones are always new, opened ones go beside them")
{
    Documents docs;
    CHECK(docs.empty());
    CHECK(docs.activeId() == kNoDocument);

    docs.open(fromFile("/a/cover.komal"));
    CHECK(docs.size() == 1);
    CHECK(docs.active().name() == "cover");
    docs.create();                             // as many blank ones as asked for
    docs.create();
    CHECK(docs.size() == 3);
    CHECK(docs.active().name() == "Untitled 2");
    CHECK(docs.active().blank());
    docs.open(fromFile("/a/back.komal"));      // beside them, none replaced
    CHECK(docs.size() == 4);
    CHECK(docs.active().name() == "back");
    CHECK(docs.all()[0]->name() == "cover");
}

TEST_CASE("activating and closing: the one after, else before; never none open")
{
    Documents docs;
    edit(docs.create());
    const DocumentId a = docs.active().id();
    edit(docs.create());
    const DocumentId b = docs.active().id();
    edit(docs.create());
    const DocumentId c = docs.active().id();
    CHECK(docs.anyUnsaved());

    REQUIRE(docs.activate(b));
    CHECK_FALSE(docs.activate(DocumentId{ 99 }));
    REQUIRE(docs.close(b));                    // the active one: the one after it
    CHECK(docs.active().id() == c);
    REQUIRE(docs.close(c));                    // the last in the row: the one before it
    CHECK(docs.active().id() == a);
    REQUIRE(docs.activate(a));
    REQUIRE(docs.close(a));                    // the only one: none left
    CHECK(docs.empty());
    CHECK(docs.activeId() == kNoDocument);
    CHECK_FALSE(docs.anyUnsaved());
    CHECK_FALSE(docs.close(a));
    CHECK(docs.create().name() == "Untitled 4");   // names never come back
}

TEST_CASE("closing one before the active one keeps the active one active")
{
    Documents docs;
    edit(docs.create());
    const DocumentId a = docs.active().id();
    edit(docs.create());
    const DocumentId b = docs.active().id();
    REQUIRE(docs.close(a));
    CHECK(docs.active().id() == b);
}

TEST_CASE("the documents tell what changed: opened, closing (still there), activated")
{
    Documents docs;
    std::vector<std::pair<DocumentsEvent::Type, DocumentId>> heard;
    bool closingStillThere = true;
    const Connection c = docs.changed.connect([&](const DocumentsEvent& e) {
        heard.push_back({ e.type, e.id });
        if (e.type == DocumentsEvent::Type::Closing) closingStillThere &= docs.find(e.id) != nullptr;
    });
    using T = DocumentsEvent::Type;
    docs.open(fromFile("/a/cover.komal"));
    const DocumentId cover = docs.active().id();
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Opened, cover }, { T::Activated, cover } });
    heard.clear();
    const DocumentId second = docs.create().id();   // a new one beside it
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Opened, second }, { T::Activated, second } });
    heard.clear();
    docs.activate(second);                          // already: nothing
    docs.activate(cover);
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Activated, cover } });
    heard.clear();
    docs.close(cover);                              // the active one: the next one takes over
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Closing, cover }, { T::Activated, second } });
    heard.clear();
    docs.close(second);                             // the last one: none left, none active
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Closing, second } });
    CHECK(closingStillThere);
}

TEST_CASE("the documents' actions: unsaved work is never closed unasked")
{
    Documents docs;
    ActionRegistry actions;
    registerDocumentsActions(actions, docs);
    CHECK_FALSE(actions.run(Actions::DocClose));                                   // none open: nothing to close
    const DocumentId blank = DocumentId(actions.run(Actions::DocNew).made);
    const ActionResult second = actions.run(Actions::DocNew);                  // a new one, though one is blank
    CHECK(second.made == int64_t(docs.active().id()));
    CHECK(docs.size() == 2);
    REQUIRE(actions.run(Actions::DocClose, { { DocArgs::kDocument, blank } }));
    CHECK(docs.size() == 1);
    edit(docs.active());
    const DocumentId edited = docs.active().id();
    const ActionResult made = actions.run(Actions::DocNew);
    REQUIRE(made);
    CHECK(docs.size() == 2);
    REQUIRE(actions.run(Actions::DocActivate, { { DocArgs::kDocument, edited } }));
    CHECK_FALSE(actions.run(Actions::DocClose));                                   // unsaved: refused
    CHECK(docs.size() == 2);
    REQUIRE(actions.run(Actions::DocClose, { { DocArgs::kDiscard, true } }));    // told to: gone
    CHECK(docs.size() == 1);
    CHECK(docs.active().id() == DocumentId(made.made));
    CHECK(actions.run(Actions::DocClose, { { DocArgs::kDocument, DocumentId(made.made) } }));   // blank: nothing to lose
    CHECK(docs.empty());
    CHECK_FALSE(actions.run(Actions::DocActivate, { { DocArgs::kDocument, DocumentId{ 99 } } }));
}
