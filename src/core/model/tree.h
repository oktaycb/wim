// wim - a Windows-native terminal workspace for coding agents.
// Core/model: the split tree. A workspace's panes form a tree: internal
// nodes split their area horizontally (children side by side) or vertically
// (stacked); leaves are slots that hold terminal tabs.
#pragma once

#include "core/json/json.h"
#include "core/model/ids.h"

#include <string>
#include <vector>

namespace wim::model {

enum class SplitDir : int {
    Horizontal, // children side by side (a "split right" grows this way)
    Vertical,   // children stacked top to bottom
};

// Where to place a new slot relative to an existing one.
enum class Side : int { Before, After };

// A terminal tab inside a slot. Runtime state (screen buffer, ConPTY
// handles, attention) lives in the app layer, keyed by TerminalId - the
// model stays a serializable skeleton.
struct Tab {
    TerminalId id;
    std::wstring title;   // user-set tab title (empty = auto)
    std::wstring profile; // "pwsh", "powershell", "cmd", "gitbash", "wsl-..."
    std::wstring cwd;     // last reported working directory
};

struct Slot {
    SlotId id;
    std::vector<Tab> tabs;
    int active = 0; // index into tabs
};

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};

class Node {
public:
    // A leaf slot (isSlot() true) or an internal split.
    static Node leaf(Slot slot);
    static Node split(SplitDir dir, std::vector<Node> children);

    bool isSlot() const { return slot_.id.valid(); }

    // ---- leaf access (valid only when isSlot()) ----
    Slot& slot() { return slot_; }
    const Slot& slot() const { return slot_; }

    // ---- split access (valid only when !isSlot()) ----
    SplitDir dir() const { return dir_; }
    std::vector<Node>& children() { return children_; }
    const std::vector<Node>& children() const { return children_; }
    // One ratio per child, together summing to 1.
    std::vector<double>& ratios() { return ratios_; }
    const std::vector<double>& ratios() const { return ratios_; }

    // ---- queries ----
    Node* findSlot(SlotId id);
    const Node* findSlot(SlotId id) const;
    Node* parentOfSlot(SlotId id);
    void collectSlots(std::vector<SlotId>& out) const;
    std::vector<SlotId> slotList() const;
    size_t slotCount() const;

    // ---- structural edits ----
    // Splits 'existing' to make room for 'fresh' on the given side. When
    // the enclosing split already runs in 'dir', the new slot becomes a
    // sibling (ratios rebalanced); otherwise the leaf wraps into a new
    // split. Returns false when 'existing' is not in this tree.
    bool insertRelative(SlotId existing, SplitDir dir, Side side, Slot fresh);
    // Removes the slot and collapses any split left with a single child.
    // Returns false when not found. Removing the last slot leaves an empty
    // tree (isSlot() false, no children) - callers check slotCount().
    bool removeSlot(SlotId id);
    // Moves the boundary between child i and i+1 of the split that directly
    // contains both slots (they must be siblings). newRatio is child i's
    // share, clamped to [0.1, 0.9].
    bool setSiblingRatio(SlotId left, SlotId right, double newRatio);
    // Replaces this whole node with 'other' (used by collapse and move).
    void become(Node other);

    // ---- persistence ----
    json::Value toJson() const;
    static bool fromJson(const json::Value& v, Node& out, std::string& error);

private:
    void normalizeRatios();

    Slot slot_{};
    SplitDir dir_ = SplitDir::Horizontal;
    std::vector<double> ratios_;
    std::vector<Node> children_;
};

// Geometry: lays slots out inside 'area' per the tree and returns each
// slot's rectangle (doubles, DIPs at the caller's scale).
void layoutSlots(const Node& root, Rect area, std::vector<std::pair<SlotId, Rect>>& out);

// Navigation: from 'from', the nearest slot whose center lies in 'dir'
// (Left/Right/Up/Down as felt by the user). Returns an invalid SlotId at
// an edge. Implemented over the layout so nested trees behave geometrically.
enum class NavDir : int { Left, Right, Up, Down };
SlotId neighborSlot(const std::vector<std::pair<SlotId, Rect>>& slots, SlotId from, NavDir dir);

} // namespace wim::model
