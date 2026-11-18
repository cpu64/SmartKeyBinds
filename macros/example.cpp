#include "./script.hpp"

#include <iostream>
#include <atomic>

extern Context ctx;

// ============================================================
// 1. KEY MACRO REGISTRATION
// ============================================================

// Third argument:
// true  = execute in a new thread
// false = execute in the existing thread
// omitted = default behavior

REGISTER_KEY_MACRO(KEY_A, macro_a, false)
REGISTER_KEY_MACRO(KEY_B, macro_b, true)

void macro_a(input_event event)
{
    // Macro implementation
}

void macro_b(input_event event)
{
    // Macro implementation
}


// ============================================================
// 2. INPUT EVENTS
// ============================================================

REGISTER_KEY_MACRO(KEY_C, macro_c, false)

void macro_c(input_event event)
{
    // Inspect the event
    int key = event.code;
    int value = event.value;

    // Event values can be DOWN, UP, etc.

    if (event.value == DOWN)
    {
        // Key pressed
    }
    else if (event.value == UP)
    {
        // Key released
    }

    // Forward original event
    pass_event_on(event);
}


// ============================================================
// 3. MODIFYING / REMAPPING EVENTS
// ============================================================

REGISTER_KEY_MACRO(KEY_D, macro_d)

void macro_d(input_event event)
{
    // Remap D to E
    event.code = KEY_E;

    pass_event_on(event);
}


// ============================================================
// 4. SIMULATING KEY PRESSES
// ============================================================

REGISTER_KEY_MACRO(KEY_E, macro_e)

void macro_e(input_event event)
{
    // Simulate a normal key press
    key_press(KEY_A);

    // Explicit key down / up
    key_press(KEY_B, DOWN);
    key_press(KEY_B, UP);

    // Modifier combinations
    key_press(KEY_LEFTCTRL, DOWN);
    key_press(KEY_C);
    key_press(KEY_LEFTCTRL, UP);
}


// ============================================================
// 5. KEYBOARD MODIFIER HELPERS
// ============================================================

void example_modifier_combinations()
{
    // Ctrl + A
    key_press(KEY_LEFTCTRL, DOWN);
    key_press(KEY_A);
    key_press(KEY_LEFTCTRL, UP);

    // Alt + F4
    key_press(KEY_LEFTALT, DOWN);
    key_press(KEY_F4);
    key_press(KEY_LEFTALT, UP);

    // Meta + F2
    key_press(KEY_LEFTMETA, DOWN);
    key_press(KEY_F2);
    key_press(KEY_LEFTMETA, UP);

    // Ctrl + Shift + T
    key_press(KEY_LEFTCTRL, DOWN);
    key_press(KEY_LEFTSHIFT, DOWN);
    key_press(KEY_T);
    key_press(KEY_LEFTCTRL, UP);
    key_press(KEY_LEFTSHIFT, UP);

    // Alt + Shift + B
    key_press(KEY_LEFTALT, DOWN);
    key_press(KEY_LEFTSHIFT, DOWN);
    key_press(KEY_B);
    key_press(KEY_LEFTSHIFT, UP);
    key_press(KEY_LEFTALT, UP);
}


// ============================================================
// 6. CHECKING KEYBOARD STATE
// ============================================================

REGISTER_KEY_MACRO(KEY_F1, macro_keyboard_state)

void macro_keyboard_state(input_event event)
{
    // Physical keyboard state
    if (ctx.real_keyboard_state[KEY_LEFTCTRL])
    {
        // Physical Ctrl is held
    }

    // Virtual keyboard state
    if (ctx.virtual_keyboard_state[KEY_LEFTALT])
    {
        // Virtual Alt is held
    }

    pass_event_on(event);
}


// ============================================================
// 7. ACTIVE WINDOW INFORMATION
// ============================================================

REGISTER_KEY_MACRO(KEY_F2, macro_window)

void macro_window(input_event event)
{
    std::string window = ctx.GetActiveWindowName();

    if (window == "Firefox")
    {
        // Firefox-specific behavior
    }
    else if (window == "konsole")
    {
        // Konsole-specific behavior
    }

    pass_event_on(event);
}


// ============================================================
// 8. MOUSE POSITION
// ============================================================

REGISTER_KEY_MACRO(KEY_F3, macro_mouse)

void macro_mouse(input_event event)
{
    auto mouse = ctx.GetMousePosition();

    if (mouse)
    {
        int x = mouse.value().x;
        int y = mouse.value().y;

        // Use mouse coordinates
    }

    pass_event_on(event);
}


// ============================================================
// 9. IMAGE LOADING
// ============================================================

void example_load_image()
{
    overlay::Image image;

    if (load_image("./assets/example.bmp", image))
    {
        uint32_t width = image.width;
        uint32_t height = image.height;

        // Image successfully loaded
    }
}


// ============================================================
// 10. OVERLAY MANAGEMENT
// ============================================================

static int overlay_id = -1;

void example_create_overlay()
{
    overlay::Image image;

    if (load_image("./assets/example.bmp", image))
    {
        overlay_id = ctx.overlay_manager.create(
            image,
            image.width,
            image.height
        );
    }
}

void example_show_overlay()
{
    if (overlay_id >= 0)
    {
        // Show overlay at screen coordinates
        ctx.overlay_manager.show(overlay_id, 500, 300);
    }
}

void example_hide_overlay()
{
    if (overlay_id >= 0)
    {
        ctx.overlay_manager.hide(overlay_id);
    }
}


// ============================================================
// 11. TIMING / DELAYS
// ============================================================

REGISTER_KEY_MACRO(KEY_F4, macro_timing)

void macro_timing(input_event event)
{
    key_press(KEY_A);

    // Wait 100 milliseconds
    msleep(100);

    key_press(KEY_B);

    // Wait 500 milliseconds
    msleep(500);

    key_press(KEY_C);
}


// ============================================================
// 12. EVENT TIMESTAMPS
// ============================================================

REGISTER_KEY_MACRO(KEY_F5, macro_timestamp)

void macro_timestamp(input_event event)
{
    // Event timestamp in milliseconds
    int64_t timestamp =
    (int64_t)event.time.tv_sec * 1000 +
    event.time.tv_usec / 1000;

    // Use timestamp for timing calculations

    pass_event_on(event);
}


// ============================================================
// 13. SHARED STATE / ATOMIC VARIABLES
// ============================================================

// Useful when macros execute in separate threads

static std::atomic<int> shared_counter = 0;

REGISTER_KEY_MACRO(KEY_F6, macro_shared_state, true)

void macro_shared_state(input_event event)
{
    if (event.value == DOWN)
    {
        shared_counter++;

        int current = shared_counter.load();
    }
}
