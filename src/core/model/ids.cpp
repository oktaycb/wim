#include "ids.h"

#include <atomic>

namespace wim {

uint64_t IdGenerator::next()
{
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

WorkspaceId newWorkspaceId() { return WorkspaceId{IdGenerator::next()}; }
SlotId newSlotId() { return SlotId{IdGenerator::next()}; }
TerminalId newTerminalId() { return TerminalId{IdGenerator::next()}; }

} // namespace wim
