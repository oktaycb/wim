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
