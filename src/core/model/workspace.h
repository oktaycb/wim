// wim - a Windows-native terminal workspace for coding agents.
// Core/model: workspaces and the workspace collection. Pure model - the
// app layer owns processes, buffers and attention; this file is what a
// session file saves and restores.
#pragma once

#include "core/model/ids.h"
#include "core/model/tree.h"

#include <string>
#include <deque>
#include <vector>

namespace wim::model {

struct Workspace {
    WorkspaceId id;
    std::wstring name;   // shown in the sidebar
    std::wstring folder; // starting cwd of its panes
    int tileIndex = 0;   // letter-tile hue, cycled per workspace
    Node root;           // split tree (may be empty after the last slot closed)
    SlotId activeSlot{}; // focused slot when this workspace is active

    json::Value toJson() const;
    static bool fromJson(const json::Value& v, Workspace& out, std::string& error);

    // Finds the slot that holds the given terminal (tab), or null.
    const Node* slotForTerminal(TerminalId id) const;
    Node* slotForTerminal(TerminalId id);
    // All terminals across all tabs, in visual order.
    std::vector<TerminalId> terminalList() const;
    // First slot for focus purposes: activeSlot if present, else first.
    SlotId focusedSlot() const;
};

// The whole model: an ordered list of workspaces and which is active.
class AppModel {
public:
    // Creates a workspace with one slot and one tab on 'profile' starting
    // in 'folder'. Name defaults to the folder's last component.
    Workspace& addWorkspace(const std::wstring& folder, const std::wstring& profile,
                            const std::wstring& name = {});

    std::deque<Workspace>& workspaces() { return workspaces_; }
    const std::deque<Workspace>& workspaces() const { return workspaces_; }

    Workspace* find(WorkspaceId id);
    const Workspace* find(WorkspaceId id) const;

    size_t activeIndex() const { return active_; }
    void setActiveIndex(size_t index); // clamps
    Workspace* active() { return workspaces_.empty() ? nullptr : &workspaces_[active_]; }
    const Workspace* active() const { return workspaces_.empty() ? nullptr : &workspaces_[active_]; }

    // Moves a workspace one step (delta -1/+1); returns false at the edges.
    bool moveWorkspace(WorkspaceId id, int delta);

    // Removes a workspace; the active index adjusts. Returns false if not
    // found. The model never keeps zero workspaces: closing the last one is
    // the caller's decision (the app replaces it with a fresh one).
    bool removeWorkspace(WorkspaceId id);

    // The workspace containing a terminal (tab), or null.
    Workspace* workspaceForTerminal(TerminalId id);
    const Workspace* workspaceForTerminal(TerminalId id) const;

    json::Value toJson() const;
    bool fromJson(const json::Value& v, std::string& error);

private:
    // deque: addWorkspace returns a reference, and references must stay
    // valid while more workspaces are added.
    std::deque<Workspace> workspaces_;
    size_t active_ = 0;
};

// "C:\dev\wim" -> "wim"; a drive root -> "C:"; empty -> "home".
std::wstring nameFromFolder(const std::wstring& folder);

} // namespace wim::model
