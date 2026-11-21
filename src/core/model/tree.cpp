#include "tree.h"
#include "core/util/str.h"

#include <algorithm>
#include <cmath>

namespace wim::model {

namespace {
constexpr double kMinRatio = 0.1;
constexpr double kMaxRatio = 0.9;
} // namespace

Node Node::leaf(Slot slot)
{
    Node n;
    n.slot_ = std::move(slot);
    return n;
}

Node Node::split(SplitDir dir, std::vector<Node> children)
{
    Node n;
    n.dir_ = dir;
    n.children_ = std::move(children);
    n.normalizeRatios();
    return n;
}

void Node::normalizeRatios()
{
    ratios_.assign(children_.size(), 1.0 / double(children_.size()));
}

// ---- queries -----------------------------------------------------------------

Node* Node::findSlot(SlotId id)
{
    if (isSlot())
        return slot_.id == id ? this : nullptr;
    for (auto& child : children_) {
        if (Node* hit = child.findSlot(id))
            return hit;
    }
    return nullptr;
}

const Node* Node::findSlot(SlotId id) const
{
    return const_cast<Node*>(this)->findSlot(id);
}

Node* Node::parentOfSlot(SlotId id)
{
    if (isSlot())
        return nullptr;
    for (auto& child : children_) {
        if (child.isSlot() && child.slot_.id == id)
            return this;
        if (Node* hit = child.parentOfSlot(id))
            return hit;
    }
    return nullptr;
}

void Node::collectSlots(std::vector<SlotId>& out) const
{
    if (isSlot()) {
        out.push_back(slot_.id);
        return;
    }
    for (const auto& child : children_)
        child.collectSlots(out);
}

std::vector<SlotId> Node::slotList() const
{
    std::vector<SlotId> out;
    collectSlots(out);
    return out;
}

size_t Node::slotCount() const
{
    if (isSlot())
        return 1;
    size_t n = 0;
    for (const auto& child : children_)
        n += child.slotCount();
    return n;
}

// ---- structural edits ----------------------------------------------------------

bool Node::insertRelative(SlotId existing, SplitDir dir, Side side, Slot fresh)
{
    Node* target = findSlot(existing);
    if (!target)
        return false;

    Node* parent = parentOfSlot(existing);
    if (parent && parent->dir_ == dir) {
        // Become a sibling: insert next to the existing slot inside parent.
        const auto it = std::find_if(parent->children_.begin(), parent->children_.end(),
                                     [&](const Node& c) { return c.isSlot() && c.slot_.id == existing; });
        const size_t index = size_t(it - parent->children_.begin());
        const size_t insertAt = side == Side::After ? index + 1 : index;
        parent->children_.insert(parent->children_.begin() + long long(insertAt), Node::leaf(std::move(fresh)));
        // Steal an equal share from every existing child.
        const size_t n = parent->children_.size();
        const double share = 1.0 / double(n);
        parent->ratios_.assign(n, share);
        return true;
    }

    // Wrap the leaf in a new split of the requested direction.
    Slot old = target->slot_;
    std::vector<Node> kids;
    if (side == Side::After) {
        kids.push_back(Node::leaf(std::move(old)));
        kids.push_back(Node::leaf(std::move(fresh)));
    } else {
        kids.push_back(Node::leaf(std::move(fresh)));
        kids.push_back(Node::leaf(std::move(old)));
    }
    *target = Node::split(dir, std::move(kids));
    return true;
}

void Node::become(Node other)
{
    slot_ = std::move(other.slot_);
    dir_ = other.dir_;
    ratios_ = std::move(other.ratios_);
    children_ = std::move(other.children_);
}

bool Node::removeSlot(SlotId id)
{
    if (isSlot()) {
        if (slot_.id != id)
            return false;
        // Removing the only slot empties the tree.
        slot_ = Slot{};
        return true;
    }

    for (size_t i = 0; i < children_.size(); ++i) {
        Node& child = children_[i];
        if (child.isSlot() && child.slot_.id == id) {
            children_.erase(children_.begin() + long long(i));
            if (children_.size() == 1) {
                // Collapse: a split with one child is that child.
                Node only = std::move(children_[0]);
                become(std::move(only));
            } else {
                normalizeRatios();
            }
            return true;
        }
        if (child.removeSlot(id)) {
            // A deeper removal may have collapsed the child into a slot
            // (nothing to fix here - collapse already happened in place).
            if (!child.isSlot() && child.children_.size() == 1)
                child.become(std::move(child.children_[0]));
            return true;
        }
    }
    return false;
}

bool Node::setSiblingRatio(SlotId left, SlotId right, double newRatio)
{
    if (isSlot())
        return false;
    int li = -1, ri = -1;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].isSlot() && children_[i].slot_.id == left)
            li = int(i);
        if (children_[i].isSlot() && children_[i].slot_.id == right)
            ri = int(i);
    }
    if (li < 0 || ri < 0 || ri != li + 1)
        return false;
    newRatio = std::clamp(newRatio, kMinRatio, kMaxRatio);
    ratios_[size_t(li)] = newRatio;
    ratios_[size_t(ri)] = 1.0 - newRatio;
    return true;
}

// ---- persistence ----------------------------------------------------------------

json::Value Node::toJson() const
{
    using json::Value;
    if (isSlot()) {
        Value v = Value::object();
        v.set("id", double(slot_.id.value));
        Value tabs = Value::array();
        for (const auto& tab : slot_.tabs) {
            Value t = Value::object();
            t.set("id", double(tab.id.value));
            t.set("title", tab.title);
            t.set("profile", tab.profile);
            t.set("cwd", tab.cwd);
            tabs.push(std::move(t));
        }
        v.set("tabs", std::move(tabs));
        v.set("active", double(slot_.active));
        return v;
    }
    Value v = Value::object();
    v.set("dir", dir_ == SplitDir::Horizontal ? "h" : "v");
    Value kids = Value::array();
    for (const auto& child : children_)
        kids.push(child.toJson());
    v.set("children", std::move(kids));
    Value rs = Value::array();
    for (const double r : ratios_)
        rs.push(r);
    v.set("ratios", std::move(rs));
    return v;
}

bool Node::fromJson(const json::Value& v, Node& out, std::string& error)
{
    out = Node{};
    if (!v.isObject()) {
        error = "node must be an object";
        return false;
    }
    if (v.contains("tabs")) { // slot
        Slot slot;
        slot.id = SlotId{uint64_t(v.get("id", 0.0))};
        if (!slot.id.valid()) {
            error = "slot id missing";
            return false;
        }
        const json::Value* tabs = v.find("tabs");
        if (!tabs || !tabs->isArray()) {
            error = "slot tabs must be an array";
            return false;
        }
        for (const auto& tv : tabs->items()) {
            Tab tab;
            tab.id = TerminalId{uint64_t(tv.get("id", 0.0))};
            tab.title = str::toWide(tv.get("title", std::string()));
            tab.profile = str::toWide(tv.get("profile", std::string()));
            tab.cwd = str::toWide(tv.get("cwd", std::string()));
            if (!tab.id.valid()) {
                error = "tab id missing";
                return false;
            }
            slot.tabs.push_back(std::move(tab));
        }
        slot.active = int(v.get("active", 0.0));
        if (slot.active < 0 || slot.active >= int(slot.tabs.size()))
            slot.active = 0;
        out = Node::leaf(std::move(slot));
        return true;
    }
    if (v.contains("children")) { // split
        const std::string d = v.get("dir", std::string("h"));
        if (d != "h" && d != "v") {
            error = "dir must be 'h' or 'v'";
            return false;
        }
        const json::Value* kids = v.find("children");
        if (!kids || !kids->isArray() || kids->items().empty()) {
            error = "split children must be a non-empty array";
            return false;
        }
        std::vector<Node> children;
        for (const auto& kv : kids->items()) {
            Node child;
            if (!fromJson(kv, child, error))
                return false;
            children.push_back(std::move(child));
        }
        out = Node::split(d == "h" ? SplitDir::Horizontal : SplitDir::Vertical,
                          std::move(children));
        // Optional persisted ratios must match the child count.
        if (const json::Value* rs = v.find("ratios"); rs && rs->isArray() &&
                                                      rs->items().size() == children.size()) {
            double sum = 0;
            for (size_t i = 0; i < rs->items().size(); ++i) {
                out.ratios_[i] = std::clamp(rs->items()[i].asNumber(1.0), kMinRatio, kMaxRatio);
                sum += out.ratios_[i];
            }
            if (sum <= 0)
                out.normalizeRatios();
            else
                for (auto& r : out.ratios_)
                    r /= sum;
        }
        return true;
    }
    error = "node has neither tabs nor children";
    return false;
}

} // namespace wim::model
