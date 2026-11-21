// wim - a Windows-native terminal workspace for coding agents.
// Core/model: strongly typed identifiers for workspaces, slots (a split
// position in the tree) and terminals (what the user and the CLI call a
// "pane": every tab is its own terminal with its own id).
#pragma once

#include <cstdint>

namespace wim {

struct WorkspaceId {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
};

struct SlotId {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
};

struct TerminalId {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
};

inline bool operator==(WorkspaceId a, WorkspaceId b) { return a.value == b.value; }
inline bool operator!=(WorkspaceId a, WorkspaceId b) { return !(a == b); }
inline bool operator==(SlotId a, SlotId b) { return a.value == b.value; }
inline bool operator!=(SlotId a, SlotId b) { return !(a == b); }
inline bool operator==(TerminalId a, TerminalId b) { return a.value == b.value; }
inline bool operator!=(TerminalId a, TerminalId b) { return !(a == b); }

// Process-wide monotonically increasing ids. Ids are never reused within a
// run, so a stale id from a closed pane can never address a new pane.
class IdGenerator {
public:
    static uint64_t next();
};

WorkspaceId newWorkspaceId();
SlotId newSlotId();
TerminalId newTerminalId();

} // namespace wim
