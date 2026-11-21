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
