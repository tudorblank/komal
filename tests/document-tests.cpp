// The documents open at once: each a workspace with its own actions and history, and its file.
// A blank slate is replaced by the next one made or opened; an edited one stays. Unsaved: its
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
    CHECK_FALSE(d.blank());                        // ... but it was edited: it stays when another comes
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
    Document& one = docs.active();
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

TEST_CASE("the blank slate gives way; an edited one stays")
{
    Documents docs;
    REQUIRE(docs.size() == 1);
    const DocumentId first = docs.active().id();
    CHECK(docs.active().name() == "Untitled 1");
    CHECK(docs.active().blank());

    CHECK(&docs.create() == &docs.active());   // still the blank one: nothing new
    CHECK(docs.size() == 1);
    CHECK(docs.active().id() == first);

    docs.open(fromFile("/a/cover.komal"));     // opened over the blank slate: it's gone
    CHECK(docs.size() == 1);
    CHECK(docs.active().name() == "cover");
    CHECK_FALSE(docs.find(first));

    edit(docs.active());
    docs.create();                             // the edited one stays
    CHECK(docs.size() == 2);
    CHECK(docs.active().name() == "Untitled 2");
    CHECK(docs.all()[0]->name() == "cover");
    docs.open(fromFile("/a/back.komal"));      // over "Untitled 2": it was still blank
    CHECK(docs.size() == 2);
    CHECK(docs.active().name() == "back");
}

TEST_CASE("activating and closing: the one after, else before; never none open")
{
    Documents docs;
    edit(docs.active());
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
    REQUIRE(docs.close(a));                    // the only one: a new blank slate
    REQUIRE(docs.size() == 1);
    CHECK(docs.active().blank());
    CHECK(docs.active().name() == "Untitled 4");   // names never come back
    CHECK_FALSE(docs.anyUnsaved());
    CHECK_FALSE(docs.close(a));
}

TEST_CASE("closing one before the active one keeps the active one active")
{
    Documents docs;
    edit(docs.active());
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
    const DocumentId first = docs.active().id();
    docs.create();                                  // blank: nothing happens
    CHECK(heard.empty());
    docs.open(fromFile("/a/cover.komal"));          // over the blank slate
    const DocumentId cover = docs.active().id();
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Closing, first }, { T::Opened, cover }, { T::Activated, cover } });
    heard.clear();
    const DocumentId second = docs.create().id();   // cover isn't blank (it has a file): a new one beside it
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Opened, second }, { T::Activated, second } });
    heard.clear();
    docs.activate(second);                          // already: nothing
    docs.activate(cover);
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Activated, cover } });
    heard.clear();
    docs.close(cover);                              // the active one: the next one takes over
    CHECK(heard == std::vector<std::pair<T, DocumentId>>{ { T::Closing, cover }, { T::Activated, second } });
    heard.clear();
    docs.close(second);                             // the last one: a new blank slate
    REQUIRE(heard.size() == 3);
    CHECK(heard[0] == std::pair{ T::Closing, second });
    CHECK(heard[1].first == T::Opened);
    CHECK(heard[2].first == T::Activated);
    CHECK(closingStillThere);
}

TEST_CASE("the documents' actions: unsaved work is never closed unasked")
{
    Documents docs;
    ActionRegistry actions;
    registerDocumentsActions(actions, docs);
    CHECK(actions.run(Actions::DocNew).made == int64_t(docs.active().id()));   // blank already: the same one
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
    CHECK(docs.size() == 1);
    CHECK_FALSE(actions.run(Actions::DocActivate, { { DocArgs::kDocument, DocumentId{ 99 } } }));
}
