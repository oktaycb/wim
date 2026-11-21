// Tests for workspace.h: collection ops, lookup, round trips.
#include "core/model/workspace.h"
#include "test.h"

using namespace wim;
using namespace wim::model;

WIM_TEST(NameFromFolder)
{
    WIM_CHECK(nameFromFolder(L"C:\\dev\\wim") == L"wim");
    WIM_CHECK(nameFromFolder(L"C:\\dev\\wim\\") == L"wim");
    WIM_CHECK(nameFromFolder(L"C:\\") == L"C:");
    WIM_CHECK(nameFromFolder(L"") == L"home");
    WIM_CHECK(nameFromFolder(L"deep\\nested\\folder") == L"folder");
}

WIM_TEST(AppModelAddAndNavigate)
{
    AppModel m;
    Workspace& a = m.addWorkspace(L"C:\\dev\\alpha", L"pwsh");
    WIM_CHECK(a.id.valid());
    WIM_CHECK(a.name == L"alpha");
    WIM_CHECK_EQ(a.root.slotCount(), size_t(1));
    WIM_CHECK(a.activeSlot.valid());
    const auto terms = a.terminalList();
    WIM_CHECK_EQ(terms.size(), size_t(1));

    Workspace& b = m.addWorkspace(L"C:\\dev\\beta", L"cmd");
    WIM_CHECK_EQ(m.workspaces().size(), size_t(2));
    WIM_CHECK_EQ(m.activeIndex(), size_t(1));
    WIM_CHECK(m.active()->id == b.id);
    WIM_CHECK_EQ(a.tileIndex, 0);
    WIM_CHECK_EQ(b.tileIndex, 1);

    m.setActiveIndex(0);
    WIM_CHECK(m.active()->id == a.id);
    m.setActiveIndex(99); // clamps
    WIM_CHECK(m.active()->id == b.id);

    WIM_CHECK(m.find(a.id) != nullptr);
    WIM_CHECK(m.find(WorkspaceId{999}) == nullptr);
}

WIM_TEST(AppModelMoveAndRemove)
{
    AppModel m;
    const WorkspaceId a = m.addWorkspace(L"C:\\a", L"pwsh").id;
    const WorkspaceId b = m.addWorkspace(L"C:\\b", L"pwsh").id;
    const WorkspaceId c = m.addWorkspace(L"C:\\c", L"pwsh").id;
    m.setActiveIndex(2);

    WIM_CHECK(m.moveWorkspace(c, -1)); // a, c, b
    WIM_CHECK_EQ(m.workspaces()[1].id.value, c.value);
    WIM_CHECK_EQ(m.activeIndex(), size_t(1)); // active followed c
    WIM_CHECK(!m.moveWorkspace(a, -1));       // edge

    WIM_CHECK(m.removeWorkspace(b)); // a, c
    WIM_CHECK_EQ(m.workspaces().size(), size_t(2));
    WIM_CHECK_EQ(m.activeIndex(), size_t(1));
    WIM_CHECK(m.removeWorkspace(a));
    WIM_CHECK(m.removeWorkspace(c));
    WIM_CHECK(m.workspaces().empty());
    WIM_CHECK(m.active() == nullptr);
    WIM_CHECK(!m.removeWorkspace(c)); // already gone
}

WIM_TEST(AppModelTerminalLookup)
{
    AppModel m;
    Workspace& a = m.addWorkspace(L"C:\\a", L"pwsh");
    const TerminalId t1 = a.root.slot().tabs[0].id;
    // Split and find each terminal's workspace even after nesting.
    Slot extra;
    extra.id = newSlotId();
    Tab t2;
    t2.id = newTerminalId();
    extra.tabs.push_back(t2);
    a.root.insertRelative(a.activeSlot, SplitDir::Horizontal, Side::After, std::move(extra));

    Workspace& b = m.addWorkspace(L"C:\\b", L"cmd");
    const TerminalId t3 = b.root.slot().tabs[0].id;

    WIM_CHECK(m.workspaceForTerminal(t1) != nullptr && m.workspaceForTerminal(t1)->id == a.id);
    WIM_CHECK(m.workspaceForTerminal(t2.id) != nullptr && m.workspaceForTerminal(t2.id)->id == a.id);
    WIM_CHECK(m.workspaceForTerminal(t3) != nullptr && m.workspaceForTerminal(t3)->id == b.id);
    WIM_CHECK(m.workspaceForTerminal(TerminalId{4242}) == nullptr);
    WIM_CHECK_EQ(a.terminalList().size(), size_t(2));
}

WIM_TEST(AppModelSerializeRoundTrip)
{
    AppModel m;
    Workspace& a = m.addWorkspace(L"C:\\dev\\alpha", L"pwsh");
    a.name = L"renamed";
    Slot extra;
    extra.id = newSlotId();
    Tab t;
    t.id = newTerminalId();
    t.profile = L"gitbash";
    t.cwd = L"C:\\dev";
    extra.tabs.push_back(t);
    a.root.insertRelative(a.activeSlot, SplitDir::Vertical, Side::After, std::move(extra));
    m.addWorkspace(L"C:\\dev\\beta", L"cmd");
    m.setActiveIndex(0);

    const std::string text = json::serialize(m.toJson(), true);
    AppModel back;
    std::string err;
    WIM_CHECK(back.fromJson(json::parse(text).value, err));
    WIM_CHECK(err.empty());
    WIM_CHECK_EQ(back.workspaces().size(), size_t(2));
    WIM_CHECK(back.workspaces()[0].name == L"renamed");
    WIM_CHECK_EQ(back.workspaces()[0].root.slotCount(), size_t(2));
    WIM_CHECK_EQ(back.activeIndex(), size_t(0));
    const auto terms = back.workspaces()[0].terminalList();
    WIM_CHECK_EQ(terms.size(), size_t(2));

    // Trash is rejected with an error, not a crash.
    json::Value bad = json::Value::object();
    bad.set("workspaces", 5);
    WIM_CHECK(!back.fromJson(bad, err));
    WIM_CHECK(!err.empty());
}
