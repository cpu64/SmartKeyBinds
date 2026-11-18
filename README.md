# SmartKeyBinds

> \[\!WARNING\] **Work in Progress — Complete Rewrite** SmartKeyBinds is currently being rewritten from the ground up, migrating the original [LX-macros](<https://github.com/NewHere3/LX-macros>) project from **X11 to Wayland (KDE Plasma / KWin)**.
>
> This repository represents an ongoing development effort. APIs, architecture, features, and implementation details are subject to change. The project is not yet considered stable or feature-complete.

**A context-aware keyboard interception and remapping framework for KDE Plasma / KWin.**

SmartKeyBinds is the Wayland successor to [LX-macros](<https://github.com/NewHere3/LX-macros>), a context-aware keyboard macro and remapping system originally developed for X11.

The goal of this rewrite is to preserve the flexibility and programmability of the original project while adapting its architecture to Wayland.

SmartKeyBinds allows users to customize keyboard behavior using statically compiled C++ macros. Unlike traditional keyboard remappers, it can make decisions based on desktop state, such as the currently active window.

Users write C++ functions that define what should happen when specific keys are pressed, with access to input and context APIs provided by the project.

## Features

- **C++ macros:** Define keyboard behavior using native C++ code.
- **Context awareness:** Make key behavior depend on information exposed by KWin.
- **Full input interception:** Take exclusive control of managed physical input devices.
- **Virtual input devices:** Reproduce keyboard events through virtual devices.
- **Default passthrough:** Keys without custom handlers retain their original behavior.
- **Extensible context providers:** KWin is the initial provider; additional providers can be implemented in the future.
- **Convenient API:** Macros use high-level functions for interacting with keyboard input rather than dealing directly with Linux input events.

## Rewrite goals

The original LX-macros project was built around X11. This rewrite aims to bring its functionality to Wayland without relying on X11-specific mechanisms.

Key goals include:

- Integrate with KWin to obtain desktop context that is otherwise inaccessible to ordinary Wayland applications.
- Preserve the flexibility of writing arbitrary C++ macro logic.
- Provide a cleaner and more extensible architecture for future development.

## Architecture

SmartKeyBinds consists of three main components.

### 1\. KWin Plugin

The KWin plugin provides access to desktop state and accepts commands from the engine.

Its primary responsibility is exposing information that macros can use to make context-dependent decisions.

The initial implementation exposes:

- Active window information (`active_window`).
- Other KWin state and operations as they are implemented.

The plugin communicates with the engine through the shared IPC protocol.

### 2\. Engine

The engine is responsible for keyboard input processing.

It:

- Discovers and manages physical input devices.
- Acquires exclusive access to managed devices.
- Captures physical keyboard events.
- Dispatches events to user-defined macros.
- Provides an API for generating keyboard events.
- Creates virtual input devices using Linux `uinput`.
- Passes through events that are not overridden.
- Communicates with context providers.

The engine is independent of KWin's internal implementation. It communicates with KWin through the IPC layer.

### 3\. Macros

Macros are user-written C++ code compiled and statically linked into the engine.

A macro defines the behavior of a key or key combination. It can inspect available context and perform actions through the SmartKeyBinds API.

For example, a user could define a binding that behaves differently depending on which application is active.

## Example macro

The following is illustrative API syntax:

cpp

```cpp
#include <smartkeybinds/macros.hpp>

void on_key_press(KeyCode key, const Context& context)
{
    if (key == KEY_CAPSLOCK)
    {
        if (context.active_window.class_name == "Firefox")
        {
            press_key(KEY_ESC);
        }
        else
        {
            pass_through();
        }
    }
}
```

The actual API and context property names will be defined as the project develops.

## Input handling

SmartKeyBinds uses Linux input devices as its input source and virtual input devices as its output.

The engine takes exclusive control of managed physical devices and forwards resulting events through virtual devices.

This allows SmartKeyBinds to intercept and modify keyboard behavior without applications receiving duplicate physical and virtual events.

Keys without custom behavior are forwarded unchanged.

## Project structure

text

```text
.
├── common/
│   └── ipc/
│       ├── protocol/
│       ├── client.cpp
│       ├── server.cpp
│       └── README.md
├── engine/
│   ├── CMakeLists.txt
│   └── main.cpp
├── kwin-plugin/
│   ├── bridge.cpp
│   ├── bridge.hpp
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── metadata.json
├── macros/
├── CMakeLists.txt
└── Makefile
```

## Requirements

- Linux with the evdev input subsystem.
- Linux `uinput` support.
- KDE Plasma with KWin.
- Appropriate permissions to access and manage input devices.
- C++ compiler and CMake.

## Building

Build instructions will be documented as the build system and installation process are finalized.

## Design philosophy

SmartKeyBinds aims to separate three concerns:

- **Context:** What is happening in the desktop environment?
- **Input:** What physical input events are being received?
- **Behavior:** What should happen when a particular key is pressed?

The engine handles input, plugins provide context, and macros define behavior.

This separation keeps the macro API simple while allowing the underlying implementation to evolve.
