# SmartKeyBinds Functional Specification

**Version:** 0.1\
**Status:** Initial design

## 1\. Purpose

SmartKeyBinds provides a programmable keyboard interception and remapping system for Linux, initially targeting KDE Plasma and KWin.

Users define keyboard behavior using statically compiled C++ functions.

The system allows keyboard behavior to depend on desktop context, such as the active window, while maintaining complete control over the input events delivered to the rest of the system.

## 2\. Core requirements

The system must satisfy the following requirements:

- Capture input from managed physical keyboard devices.
- Prevent original events from independently reaching normal input consumers.
- Generate virtual input events.
- Allow users to override individual keys and key combinations.
- Pass through events without custom handlers.
- Expose desktop context to user-defined macros.
- Allow macros to execute multiple actions.
- Support additional context providers in the future.
- Keep macro authors independent of low-level Linux input implementation.

## 3\. Macro system

### 3.1 Implementation

Macros are written in C++.

Each macro is compiled and statically linked into the engine.

The user does not write configuration files describing individual key mappings. Instead, they implement functions that define the desired behavior.

The project provides headers, types, and helper functions to make macro development convenient.

### 3.2 Macro registration

The engine needs a mechanism to identify which functions handle which keys.

A possible registration interface:

cpp

```cpp
REGISTER_KEY_HANDLER(KEY_CAPSLOCK, on_capslock);
REGISTER_KEY_HANDLER(KEY_ESC, on_escape);
```

Alternatively, the engine could expose a single dispatch function:

cpp

```cpp
void on_key_event(const KeyEvent& event, const Context& context)
{
    switch (event.key)
    {
        case KEY_CAPSLOCK:
            on_capslock(event, context);
            break;

        case KEY_ESC:
            on_escape(event, context);
            break;

        default:
            pass_through();
            break;
    }
}
```

The final implementation should favor an explicit and predictable registration mechanism.

### 3.3 Event handling

A macro must be able to distinguish between:

- Key press.
- Key release.
- Key repeat.

This is important because many keyboard operations require maintaining state between press and release events.

The macro interface should provide a structured event:

cpp

```cpp
struct KeyEvent
{
    KeyCode key;
    KeyState state;
    DeviceId device;
};
```

The exact fields remain subject to implementation.

### 3.4 Default behavior

The default behavior of every key is passthrough.

If a key does not have an overriding handler, its original event must be reproduced through a virtual device.

If a macro explicitly chooses passthrough, the engine must reproduce the appropriate original event.

If a macro consumes an event, the original event must not be reproduced.

The engine must ensure that press and release events remain consistent.

For example, consuming a key press but forwarding its release could cause incorrect input state in applications.

### 3.5 Macro API

The project should provide a convenient abstraction over virtual input.

Initial API candidates:

| Function | Description |
| --- | --- |
| `press_key(code)` | Emit a key press |
| `release_key(code)` | Emit a key release |
| `tap_key(code)` | Emit a complete key press/release |
| `press_keys(...)` | Press multiple keys |
| `release_keys(...)` | Release multiple keys |
| `pass_through()` | Forward the original event |
| `consume_event()` | Suppress the original event |
| `send_text(text)` | Type a string |
| `get_context()` | Access current desktop context |

The API must define how generated key states interact with physical key states.

For example, if the user holds Shift physically while a macro emits another key, the engine must not accidentally release Shift when the macro finishes.

### 3.6 Key combinations

The macro system should support key combinations.

Examples:

- Ctrl + C
- Alt + Tab
- Super + K
- Custom combinations involving multiple modifiers.

The engine must track the state of physical keys and modifiers.

A combination should be recognized independently of the order in which its keys are pressed, unless a macro explicitly requests sequence-sensitive behavior.

### 3.7 Multiple actions

A macro can perform multiple actions in response to one input event.

Example:

cpp

```cpp
void on_capslock(const KeyEvent& event, const Context& context)
{
    if (event.state != KeyState::Pressed)
        return;

    press_key(KEY_LEFTCTRL);
    tap_key(KEY_TAB);
    release_key(KEY_LEFTCTRL);
}
```

This is illustrative and assumes the API supports appropriate key-state handling.

The engine should define how actions are ordered and how errors are handled.

## 4\. Context awareness

### 4.1 General behavior

Macros can access context information exposed by registered context providers.

The user decides which context properties are relevant to each macro.

There should be no requirement that all macros use the same context information.

### 4.2 KWin context provider

The initial provider is the KWin plugin.

Its first responsibility is exporting active window information.

Potential active window properties include:

- Window identifier.
- Window title.
- Application name.
- Application class.
- Window state.
- Other useful window metadata.

The exact list is TBD.

### 4.3 Context updates

The engine should receive context updates when relevant desktop state changes.

For example, when the active window changes:

1. KWin detects the focus change.
2. The plugin obtains the updated window information.
3. The plugin sends a state update through IPC.
4. The engine updates its context state.
5. Subsequent macro executions can use the updated state.

The engine should avoid requiring synchronous IPC requests for every keyboard event.

Context should normally be maintained as locally cached state.

### 4.4 Missing context

A provider may be unavailable or temporarily disconnected.

Macros must be able to detect unavailable context rather than accidentally treating missing information as valid data.

The specification should eventually define whether a macro can:

- Continue with reduced context.
- Use a fallback behavior.
- Pass through the original input.
- Explicitly consume the event.

## 5\. Input subsystem

### 5.1 Physical input

The engine manages physical keyboard devices through Linux evdev.

It should discover available devices and acquire exclusive access to managed devices.

The intended behavior is that normal input consumers receive events only from SmartKeyBinds virtual devices, not directly from the managed physical devices.

### 5.2 Exclusive access

The engine should use Linux exclusive device grabbing where supported.

The implementation must account for:

- Permission failures.
- Devices that cannot be grabbed.
- Device disconnection.
- Device reconnection.
- Multiple physical keyboards.
- Partial device initialization.

A device that cannot be acquired must not silently be treated as successfully managed.

### 5.3 Virtual input

The engine uses Linux `uinput` to create virtual devices.

Virtual devices should expose the necessary event capabilities to reproduce the input supported by their corresponding physical devices.

The engine must preserve:

- Key press events.
- Key release events.
- Repeat behavior where applicable.
- Relevant synchronization events.
- Correct ordering of emitted events.

### 5.4 Default passthrough

Passthrough means reproducing the original input through a virtual device.

It does not mean allowing the original physical event to reach applications.

This distinction is fundamental to the SmartKeyBinds input architecture.

### 5.5 Device identity

The implementation should preserve enough information about physical devices to distinguish events from different keyboards.

This is important for:

- Multiple keyboards.
- Device-specific bindings.
- Correct key-state tracking.
- Device disconnection recovery.

## 6\. KWin integration

### 6.1 Responsibilities

The KWin plugin provides:

- Desktop state.
- Active window information.
- State change notifications.
- Supported window-manager commands.

It does not perform physical keyboard interception.

### 6.2 Commands

The engine may send commands to the KWin plugin.

Examples of possible future operations:

- Focus a window.
- Minimize a window.
- Maximize a window.
- Change virtual desktop.
- Move a window.

The actual command set is TBD.

The plugin should validate commands and report success or failure.

## 7\. IPC protocol

The IPC protocol is shared between the engine and KWin plugin.

It must support:

- Connection establishment.
- Connection termination.
- State updates.
- State requests where necessary.
- Command requests.
- Command responses.
- Error reporting.
- Protocol versioning.

The protocol should be extensible so that new context properties and commands can be introduced without unnecessarily breaking existing implementations.

## 8\. Additional context providers

Although SmartKeyBinds initially targets KWin, the engine should not assume that all context originates from KWin.

Future integrations should be implemented as additional providers.

A new provider should be able to:

1. Connect to the engine.
2. Register its identity.
3. Publish available context properties.
4. Send state updates.
5. Optionally accept commands.
6. Disconnect cleanly.

The engine aggregates provider state and makes it available to macros.

This allows the project to expand without rewriting the input engine or macro execution model.

## 9\. Runtime and build behavior

### 9.1 Compilation

Macro source files are compiled with the engine.

The resulting object files are statically linked into the executable.

### 9.2 Configuration changes

Changing macro code requires rebuilding the engine.

The engine must be restarted to execute the updated macro implementation.

Dynamic macro loading and runtime interpretation are explicitly outside the initial design.

### 9.3 Initialization

The engine should initialize in a predictable order:

1. Initialize logging and core infrastructure.
2. Initialize IPC.
3. Connect to available context providers.
4. Discover physical input devices.
5. Initialize virtual input devices.
6. Acquire exclusive physical device access.
7. Initialize macro state.
8. Begin normal input processing.

The exact initialization sequence may change based on implementation requirements, particularly to minimize the time between acquiring physical devices and having working virtual outputs.

### 9.4 Shutdown

On shutdown, the engine must:

1. Stop accepting new input events.
2. Release any synthetic keys that remain pressed.
3. Clean up virtual devices.
4. Release physical device grabs.
5. Disconnect IPC connections.
6. Release allocated resources.

The implementation must also consider abnormal termination and process crashes.

## 10\. Non-functional requirements

### Performance

- Input processing should introduce minimal latency.
- Context should be locally cached.
- The input event processing path should avoid unnecessary allocations.
- Macro execution should not block input processing indefinitely.

### Reliability

- Key state must remain consistent.
- Physical and virtual device failures must be handled.
- Provider disconnections must not crash the engine.
- Shutdown must release input resources.

### Maintainability

- The engine must remain independent of KWin internals.
- The IPC protocol must be shared and independently maintained.
- Macro APIs should hide implementation details.
- New context providers should not require fundamental engine redesign.

### Safety

- Macro code is trusted native code.
- Device ownership must be explicit.
- Failure to acquire a physical device must be reported.
- Input recovery must be considered a core requirement, not an optional enhancement.

## 11\. Out of scope for initial implementation

The following are not initial requirements:

- Support for desktop environments other than KWin.
- Runtime interpreted scripting languages.
- Dynamic macro loading.
- GUI-based macro configuration.
- Network-based context providers.
- Distributed macro execution.
- Automatic macro generation.

These features may be considered later without changing the fundamental separation between input handling, context providers, and macros.

## 12\. Open technical decisions

The following decisions remain to be finalized:

1. Exact macro registration API.
2. Whether handlers are defined per key, per combination, or both.
3. Exact context data structure.
4. Complete list of KWin-exposed properties.
5. IPC transport and serialization format.
6. Whether virtual devices mirror physical device capabilities or use a shared virtual keyboard.
7. Handling of simultaneous physical and synthetic key states.
8. Macro execution threading model.
9. Behavior when a context provider becomes unavailable.
10. Device recovery and emergency input restoration mechanism.

These decisions should be made during implementation rather than prematurely restricting the architecture.
