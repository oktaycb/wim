#include "workspace.h"
#include "core/util/str.h"

#include <algorithm>

namespace wim::model {

// ---- Workspace -----------------------------------------------------------------

json::Value Workspace::toJson() const
{
    json::Value v = json::Value::object();
    v.set("id", double(id.value));
    v.set("name", name);
    v.set("folder", folder);
    v.set("tile", double(tileIndex));
    v.set("tree", root.toJson());
    v.set("activeSlot", double(activeSlot.value));
    return v;
}

bool Workspace::fromJson(const json::Value& v, Workspace& out, std::string& error)
{
    out = Workspace{};
    if (!v.isObject()) {
        error = "workspace must be an object";
        return false;
    }
    out.id = WorkspaceId{uint64_t(v.get("id", 0.0))};
    if (!out.id.valid()) {
        error = "workspace id missing";
        return false;
    }
    out.name = str::toWide(v.get("name", std::string()));
    out.folder = str::toWide(v.get("folder", std::string()));
    out.tileIndex = int(v.get("tile", double(out.id.value % 6)));
    if (const json::Value* tree = v.find("tree"); tree && !Node::fromJson(*tree, out.root, error))
        return false;
    out.activeSlot = SlotId{uint64_t(v.get("activeSlot", 0.0))};
    if (!out.root.findSlot(out.activeSlot)) {
        const auto slots = out.root.slotList();
        out.activeSlot = slots.empty() ? SlotId{} : slots.front();
    }
    return true;
}

namespace {

const Node* slotForTerminalIn(const Node& node, TerminalId id)
{
    if (node.isSlot()) {
        for (const auto& tab : node.slot().tabs)
            if (tab.id == id)
                return &node;
        return nullptr;
    }
    for (const auto& child : node.children())
        if (const Node* hit = slotForTerminalIn(child, id))
            return hit;
    return nullptr;
}

void collectTerminals(const Node& node, std::vector<TerminalId>& out)
{
    if (node.isSlot()) {
        for (const auto& tab : node.slot().tabs)
            out.push_back(tab.id);
        return;
    }
    for (const auto& child : node.children())
        collectTerminals(child, out);
}

} // namespace

const Node* Workspace::slotForTerminal(TerminalId id) const { return slotForTerminalIn(root, id); }
Node* Workspace::slotForTerminal(TerminalId id)
{
    return const_cast<Node*>(slotForTerminalIn(root, id));
}

std::vector<TerminalId> Workspace::terminalList() const
{
    std::vector<TerminalId> out;
    collectTerminals(root, out);
    return out;
}

SlotId Workspace::focusedSlot() const
{
    if (root.findSlot(activeSlot))
        return activeSlot;
    const auto slots = root.slotList();
    return slots.empty() ? SlotId{} : slots.front();
}

// ---- AppModel -------------------------------------------------------------------

Workspace& AppModel::addWorkspace(const std::wstring& folder, const std::wstring& profile,
                                  const std::wstring& name)
{
    Workspace ws;
    ws.id = newWorkspaceId();
    ws.folder = folder;
    ws.name = name.empty() ? nameFromFolder(folder) : name;
    ws.tileIndex = int((workspaces_.size()) % 6);

    Slot slot;
    slot.id = newSlotId();
    Tab tab;
    tab.id = newTerminalId();
    tab.profile = profile;
    tab.cwd = folder;
    slot.tabs.push_back(tab);
    ws.root = Node::leaf(std::move(slot));
    ws.activeSlot = ws.root.slot().id;

    workspaces_.push_back(std::move(ws));
    active_ = workspaces_.size() - 1;
    return workspaces_.back();
}

Workspace* AppModel::find(WorkspaceId id)
{
    for (auto& ws : workspaces_)
        if (ws.id == id)
            return &ws;
    return nullptr;
}

const Workspace* AppModel::find(WorkspaceId id) const
{
    return const_cast<AppModel*>(this)->find(id);
}

void AppModel::setActiveIndex(size_t index)
{
    if (workspaces_.empty()) {
        active_ = 0;
        return;
    }
    active_ = std::min(index, workspaces_.size() - 1);
}

bool AppModel::moveWorkspace(WorkspaceId id, int delta)
{
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        if (workspaces_[i].id != id)
            continue;
        const long long j = long long(i) + delta;
        if (j < 0 || j >= long long(workspaces_.size()))
            return false;
        std::swap(workspaces_[i], workspaces_[size_t(j)]);
        if (active_ == i)
            active_ = size_t(j);
        else if (active_ == size_t(j))
            active_ = i;
        return true;
    }
    return false;
}

bool AppModel::removeWorkspace(WorkspaceId id)
{
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        if (workspaces_[i].id != id)
            continue;
        workspaces_.erase(workspaces_.begin() + long long(i));
        if (active_ >= workspaces_.size())
            active_ = workspaces_.empty() ? 0 : workspaces_.size() - 1;
        return true;
    }
    return false;
}

Workspace* AppModel::workspaceForTerminal(TerminalId id)
{
    for (auto& ws : workspaces_)
        if (ws.slotForTerminal(id))
            return &ws;
    return nullptr;
}

const Workspace* AppModel::workspaceForTerminal(TerminalId id) const
{
    return const_cast<AppModel*>(this)->workspaceForTerminal(id);
}

json::Value AppModel::toJson() const
{
    json::Value v = json::Value::object();
    json::Value list = json::Value::array();
    for (const auto& ws : workspaces_)
        list.push(ws.toJson());
    v.set("workspaces", std::move(list));
    v.set("active", double(active_));
    return v;
}

bool AppModel::fromJson(const json::Value& v, std::string& error)
{
    workspaces_.clear();
    active_ = 0;
    if (!v.isObject() || !v.find("workspaces")) {
        error = "model must have a workspaces array";
        return false;
    }
    const json::Value* list = v.find("workspaces");
    if (!list->isArray()) {
        error = "workspaces must be an array";
        return false;
    }
    for (const auto& wv : list->items()) {
        Workspace ws;
        if (!Workspace::fromJson(wv, ws, error))
            return false;
        workspaces_.push_back(std::move(ws));
    }
    active_ = size_t(v.get("active", 0.0));
    if (active_ >= workspaces_.size())
        active_ = workspaces_.empty() ? 0 : workspaces_.size() - 1;
    return true;
}

std::wstring nameFromFolder(const std::wstring& folder)
{
    std::wstring f = folder;
    while (f.size() > 3 && (f.back() == L'\\' || f.back() == L'/'))
        f.pop_back();
    if (f.empty())
        return L"home";
    const size_t slash = f.find_last_of(L"\\/");
    std::wstring last = slash == std::wstring::npos ? f : f.substr(slash + 1);
    if (last.empty() && f.size() >= 2 && f[1] == L':')
        return f.substr(0, 2); // "C:\"
    if (last.empty())
        last = f;
    return last;
}

} // namespace wim::model
