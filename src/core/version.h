// wim - a Windows-native terminal workspace for coding agents.
// Core: version information.
#pragma once

namespace wim {

// Semantic version of the running build, e.g. "0.1.0".
const char* version();

// Human-readable product name: "wim <version>".
const char* versionString();

} // namespace wim
