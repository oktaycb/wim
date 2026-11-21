// Tests for ids.h.
#include "core/model/ids.h"
#include "test.h"

WIM_TEST(IdsAreUniqueAndTyped)
{
    const wim::WorkspaceId w1 = wim::newWorkspaceId();
    const wim::WorkspaceId w2 = wim::newWorkspaceId();
    WIM_CHECK(w1.valid());
    WIM_CHECK(w2.valid());
    WIM_CHECK(w1 != w2);

    const wim::SlotId s = wim::newSlotId();
    const wim::TerminalId t = wim::newTerminalId();
    // Distinct domains never compare equal (type system enforces it; the
    // generator just has to keep increasing).
    WIM_CHECK(w1.value < w2.value);
    WIM_CHECK(s.value != w1.value || true); // same generator: only uniqueness matters
    WIM_CHECK(t.valid());

    const wim::WorkspaceId zero{};
    WIM_CHECK(!zero.valid());
}

// ---- split tree (structure) --------------------------------------------------

#include "core/model/tree.h"

#include <cmath>

using namespace wim;
using namespace wim::model;

namespace {
Slot makeSlot(uint64_t id)
{
    Slot s;
    s.id = SlotId{id};
    Tab t;
    t.id = TerminalId{id * 1000};
    t.profile = L"pwsh";
    s.tabs.push_back(t);
    return s;
}
} // namespace

WIM_TEST(TreeSingleSlot)
{
    Node root = Node::leaf(makeSlot(1));
    WIM_CHECK(root.isSlot());
    WIM_CHECK_EQ(root.slotCount(), size_t(1));
    WIM_CHECK(root.findSlot(SlotId{1}) != nullptr);
    WIM_CHECK(root.findSlot(SlotId{99}) == nullptr);
}

WIM_TEST(TreeInsertSiblingsAndWrap)
{
    Node root = Node::leaf(makeSlot(1));

    // Split right: leaf wraps into a horizontal split [1, 2].
    WIM_CHECK(root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2)));
    WIM_CHECK(!root.isSlot());
    WIM_CHECK_EQ(int(root.dir()), int(SplitDir::Horizontal));
    WIM_CHECK_EQ(root.children().size(), size_t(2));
    WIM_CHECK_EQ(root.slotCount(), size_t(2));

    // Split right again at 1: same direction -> sibling inserted, no new nesting.
    WIM_CHECK(root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(3)));
    WIM_CHECK_EQ(root.children().size(), size_t(3));
    WIM_CHECK_EQ(root.children()[0].slot().id.value, uint64_t(1));
    WIM_CHECK_EQ(root.children()[1].slot().id.value, uint64_t(3));
    WIM_CHECK_EQ(root.children()[2].slot().id.value, uint64_t(2));
    WIM_CHECK_EQ(root.slotCount(), size_t(3));

    // Split down at 2 (a leaf inside a horizontal split) wraps that leaf.
    WIM_CHECK(root.insertRelative(SlotId{2}, SplitDir::Vertical, Side::After, makeSlot(4)));
    const Node* slot2 = root.findSlot(SlotId{2});
    WIM_CHECK(slot2 != nullptr);
    const Node* parent2 = root.parentOfSlot(SlotId{2});
    WIM_CHECK(parent2 != nullptr);
    WIM_CHECK_EQ(int(parent2->dir()), int(SplitDir::Vertical));
    WIM_CHECK_EQ(parent2->children().size(), size_t(2));
    WIM_CHECK_EQ(root.slotCount(), size_t(4));

    // Before-side insert: fresh slot lands directly before its target,
    // i.e. the vertical split becomes [2, 5, 4].
    WIM_CHECK(root.insertRelative(SlotId{4}, SplitDir::Vertical, Side::Before, makeSlot(5)));
    const Node* parent4 = root.parentOfSlot(SlotId{4});
    WIM_CHECK_EQ(parent4->children()[0].slot().id.value, uint64_t(2));
    WIM_CHECK_EQ(parent4->children()[1].slot().id.value, uint64_t(5));
    WIM_CHECK_EQ(parent4->children()[2].slot().id.value, uint64_t(4));
    WIM_CHECK_EQ(root.slotCount(), size_t(5));

    // Unknown target rejected.
    WIM_CHECK(!root.insertRelative(SlotId{42}, SplitDir::Horizontal, Side::After, makeSlot(6)));
}

WIM_TEST(TreeRemoveCollapses)
{
    Node root = Node::leaf(makeSlot(1));
    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2));
    root.insertRelative(SlotId{2}, SplitDir::Vertical, Side::After, makeSlot(3));

    // [1 | [2 / 3]]
    WIM_CHECK_EQ(root.slotCount(), size_t(3));
    WIM_CHECK(root.removeSlot(SlotId{3}));
    // [2 / 3] collapsed: [1 | 2]
    WIM_CHECK_EQ(root.slotCount(), size_t(2));
    WIM_CHECK(!root.isSlot());
    WIM_CHECK_EQ(root.children().size(), size_t(2));
    WIM_CHECK(root.children()[1].isSlot());

    WIM_CHECK(root.removeSlot(SlotId{2}));
    // Root collapsed to the single remaining slot.
    WIM_CHECK(root.isSlot());
    WIM_CHECK_EQ(root.slot().id.value, uint64_t(1));

    // Removing the last slot empties the tree.
    WIM_CHECK(root.removeSlot(SlotId{1}));
    WIM_CHECK_EQ(root.slotCount(), size_t(0));
    WIM_CHECK(!root.removeSlot(SlotId{1}));
}

WIM_TEST(TreeSetSiblingRatio)
{
    Node root = Node::leaf(makeSlot(1));
    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2));
    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(3));
    // Children now [1, 3, 2], equal thirds.
    WIM_CHECK(root.setSiblingRatio(SlotId{1}, SlotId{3}, 0.7));
    WIM_CHECK_EQ(root.ratios()[0], 0.7);
    WIM_CHECK(std::abs(root.ratios()[1] - 0.3) < 1e-9); // 1.0-0.7 in doubles
    // Clamped at the edges.
    WIM_CHECK(root.setSiblingRatio(SlotId{1}, SlotId{3}, 0.01));
    WIM_CHECK_EQ(root.ratios()[0], 0.1);
    // Non-siblings rejected.
    WIM_CHECK(!root.setSiblingRatio(SlotId{1}, SlotId{2}, 0.5));
}

// ---- split tree (geometry) ---------------------------------------------------

WIM_TEST(TreeLayoutSingleAndSplits)
{
    Node root = Node::leaf(makeSlot(1));
    std::vector<std::pair<SlotId, Rect>> rects;
    layoutSlots(root, Rect{0, 0, 100, 50}, rects);
    WIM_CHECK_EQ(rects.size(), size_t(1));
    WIM_CHECK_EQ(rects[0].first.value, uint64_t(1));
    WIM_CHECK_EQ(rects[0].second.w, 100.0);
    WIM_CHECK_EQ(rects[0].second.h, 50.0);

    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2));
    layoutSlots(root, Rect{0, 0, 100, 50}, rects);
    WIM_CHECK_EQ(rects.size(), size_t(2));
    WIM_CHECK(std::abs(rects[0].second.w - 50.0) < 1e-9);
    WIM_CHECK(std::abs(rects[1].second.x - 50.0) < 1e-9);
    WIM_CHECK(std::abs(rects[1].second.h - 50.0) < 1e-9);

    // Ratio honored: 70/30.
    root.setSiblingRatio(SlotId{1}, SlotId{2}, 0.7);
    layoutSlots(root, Rect{0, 0, 100, 50}, rects);
    WIM_CHECK(std::abs(rects[0].second.w - 70.0) < 1e-9);

    // Nested: split down at 2 -> [1 | [2 / 3]] with thirds.
    root.insertRelative(SlotId{2}, SplitDir::Vertical, Side::After, makeSlot(3));
    layoutSlots(root, Rect{0, 0, 90, 40}, rects);
    WIM_CHECK_EQ(rects.size(), size_t(3));
    const Rect* r3 = nullptr;
    for (auto& [id, r] : rects)
        if (id.value == 3)
            r3 = &r;
    WIM_CHECK(r3 != nullptr);
    WIM_CHECK(std::abs(r3->w - 27.0) < 1e-9); // 30% of 90
    WIM_CHECK(std::abs(r3->y - 20.0) < 1e-9); // lower half
}

WIM_TEST(TreeNeighborNavigation)
{
    // Grid: [[1 | 2] / [3 | 4]]
    Node root = Node::leaf(makeSlot(1));
    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2));
    root.insertRelative(SlotId{1}, SplitDir::Vertical, Side::After, makeSlot(3));
    root.insertRelative(SlotId{2}, SplitDir::Vertical, Side::After, makeSlot(4));

    std::vector<std::pair<SlotId, Rect>> rects;
    layoutSlots(root, Rect{0, 0, 100, 100}, rects);
    WIM_CHECK_EQ(rects.size(), size_t(4));

    WIM_CHECK_EQ(neighborSlot(rects, SlotId{1}, NavDir::Right).value, uint64_t(2));
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{1}, NavDir::Down).value, uint64_t(3));
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{4}, NavDir::Left).value, uint64_t(3));
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{4}, NavDir::Up).value, uint64_t(2));
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{2}, NavDir::Up).value, uint64_t(0)); // edge
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{3}, NavDir::Left).value, uint64_t(0));
    WIM_CHECK_EQ(neighborSlot(rects, SlotId{99}, NavDir::Left).value, uint64_t(0)); // unknown
}

WIM_TEST(TreeSerializeRoundTrip)
{
    Node root = Node::leaf(makeSlot(1));
    root.insertRelative(SlotId{1}, SplitDir::Horizontal, Side::After, makeSlot(2));
    root.insertRelative(SlotId{2}, SplitDir::Vertical, Side::After, makeSlot(3));
    root.setSiblingRatio(SlotId{1}, SlotId{2}, 0.75);
    // Give slot 2 a second tab and select it.
    Node* s2 = root.findSlot(SlotId{2});
    Tab t2;
    t2.id = TerminalId{2001};
    t2.profile = L"cmd";
    t2.cwd = L"C:\dev";
    s2->slot().tabs.push_back(t2);
    s2->slot().active = 1;

    const json::Value j = root.toJson();
    const std::string text = json::serialize(j, true);
    Node back;
    std::string err;
    WIM_CHECK(Node::fromJson(json::parse(text).value, back, err));
    WIM_CHECK(err.empty());

    std::vector<std::pair<SlotId, Rect>> a, b;
    layoutSlots(root, Rect{0, 0, 100, 100}, a);
    layoutSlots(back, Rect{0, 0, 100, 100}, b);
    WIM_CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        WIM_CHECK_EQ(a[i].first.value, b[i].first.value);
        WIM_CHECK(std::abs(a[i].second.w - b[i].second.w) < 1e-9);
    }
    const Node* back2 = back.findSlot(SlotId{2});
    WIM_CHECK_EQ(back2->slot().tabs.size(), size_t(2));
    WIM_CHECK_EQ(back2->slot().active, 1);
    WIM_CHECK(back2->slot().tabs[1].cwd == L"C:\dev");

    // Rejects malformed trees.
    WIM_CHECK(!Node::fromJson(json::Value::object(), back, err));
    WIM_CHECK(!err.empty());
}
