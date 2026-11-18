#pragma once

#include <array>
#include <cstddef>
#include <linux/input.h>

// --------------------------------------------------
// Registration macros
// --------------------------------------------------

#define REGISTER_KEY_MACRO_2(key, function) REGISTER_KEY_MACRO_3(key, function, false)

#define REGISTER_KEY_MACRO_3(key, function, threaded)                                                                  \
    void function(input_event event);                                                                                  \
    static MacroRegistrar registrar_##function(key, &function, threaded);

#define GET_MACRO(_1, _2, _3, NAME, ...) NAME

#define REGISTER_KEY_MACRO(...) GET_MACRO(__VA_ARGS__, REGISTER_KEY_MACRO_3, REGISTER_KEY_MACRO_2)(__VA_ARGS__)

// --------------------------------------------------
// Configuration
// --------------------------------------------------

constexpr std::size_t NUMBER_OF_KEYS = KEY_MAX + 1;

using MacroFunction = void (*)(input_event);

// --------------------------------------------------
// Key binding
// --------------------------------------------------

struct KeyBinding
{
    MacroFunction function = nullptr;
    bool separate_thread = false;
};

// --------------------------------------------------
// Registry
// --------------------------------------------------

class MacroRegistry
{
  public:
    static MacroRegistry &instance()
    {
        static MacroRegistry registry;
        return registry;
    }

    void register_macro(int key, MacroFunction function, bool separate_thread = false)
    {
        if (key < 0 || key >= NUMBER_OF_KEYS)
            return;

        m_bindings[key] = {function, separate_thread};
    }

    const KeyBinding &get(int key) const
    // const KeyBinding get(int key) const
    {
        static const KeyBinding Empty{};

        if (key < 0 || key >= NUMBER_OF_KEYS)
        {
            // return KeyBinding{&pass_event_on, false};
            return Empty;
        }

        return m_bindings[key];
    }

  private:
    std::array<KeyBinding, NUMBER_OF_KEYS> m_bindings{};
};

// --------------------------------------------------
// Automatic registration
// --------------------------------------------------

struct MacroRegistrar
{
    MacroRegistrar(int key, MacroFunction function, bool separate_thread = false)
    {
        MacroRegistry::instance().register_macro(key, function, separate_thread);
    }
};
