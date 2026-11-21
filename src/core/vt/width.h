// wim - a Windows-native terminal workspace for coding agents.
// Core/vt: character width for grid layout - 0 (combining/zero-width),
// 1, or 2 (East Asian wide and emoji). A condensed table, not a full
// Unicode properties database: it covers the ranges terminals actually
// render and defaults to 1.
#pragma once

#include <cstdint>

namespace wim::vt {

int charWidth(char32_t cp);

} // namespace wim::vt
