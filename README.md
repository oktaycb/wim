# wim

**wim** is a Windows-native terminal workspace for running coding agents side by side — a C++ take on the [aghouse](https://github.com/pradprat/aghouse) idea, with a vim-flavored command layer on top.

> Named after vim: `w`indows + `im`. It is a real Win32 application that also
> starts from any terminal by typing `wim`.

## Status

Under active development. This repository builds `wim.exe`, a single
executable that is both the GUI app and the CLI client.

## Goals

- **Native Win32 GUI** — no Electron, no web views. Direct2D/DirectWrite
  rendering, one swap chain per terminal pane, real ConPTY terminals.
- **Workspaces, splits and tabs** — a sidebar of workspaces; each workspace is
  a tree of split panes; each pane holds terminal tabs.
- **Agent attention** — OSC 9 / 99 / 777 escape codes, bell and `wim notify`
  all feed one attention model: accent rings, waiting list, toasts and a
  flashing taskbar button.
- **vim modes + classic shortcuts** — `j`/`k` pane motion and a `:` command
  line (`:split`, `:q`, `:theme …`) alongside Ctrl+Shift shortcuts, so both
  muscle memories work.
- **Automation** — a per-user named pipe; `wim list`, `wim send-text`,
  `wim read-pane` and friends drive the app from scripts and agents.

## Layout

```text
src/core   no-UI core: escape-code parser, split-tree model, JSON, pipe protocol, settings
src/app    Win32 app: ConPTY panes, DirectWrite terminal renderer, sidebar, palette
src/cli    the CLI half of wim.exe
tests/     unit tests for core
```

## Build

Requires Visual Studio 2022 (C++ workload), CMake 3.21+ and Ninja.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\wim.exe
```
