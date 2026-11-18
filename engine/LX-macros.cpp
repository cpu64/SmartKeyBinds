#include "headers/LX-active_window.hpp"
#include "headers/LX-global_variables.hpp"
#include "headers/LX-input.hpp"
#include "headers/LX-listener.hpp"
#include "headers/LX-messages.hpp"
#include "headers/LX-user.hpp"
#include "headers/LX-utilities.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

GlobalVariables Gvar;
GlobalVariables_ &Gvar_ = *(GlobalVariables_ *)&Gvar;

bool close_fun()
{
    while (Gvar_.unfinished_threadcount_ > 0)
    {
        msleep(100);
    }

    Message M("close", LX_MESSAGE);
    M.send(Gvar_.root_pipe_);

    fprintf(stderr, "%s.\n", strerror(errno));
    fflush(stdout);
    fflush(stderr);

    // XCloseDisplay(Gvar_.display_);
    fclose(Gvar_.root_pipe_);
    fclose(Gvar_.user_pipe_);
    return 1;
}

bool exit_fun()
{
    Message M("exit", LX_MESSAGE);
    M.send(Gvar_.root_pipe_);
    fflush(stdout);
    // exit(1);
    return 1;
}

// void kill_fun()
// void terminate_fun()

void shift_macro()
{
    int time_spent = 0;
    while (1)
    {
        time_spent = 0;
        if (Gvar.active_window_name() == "Sober")
        {
            // std::cout << "KEY_LEFTSHIFT " << Gvar.real_keyboard_state(KEY_LEFTSHIFT) << " KEY_SPACE " <<
            // Gvar.real_keyboard_state(KEY_SPACE) << std::endl;
            if (Gvar.real_keyboard_state(KEY_LEFTSHIFT))
            {
                key_press(KEY_Q);
            }
            if (Gvar.real_keyboard_state(KEY_SPACE))
            {
                time_spent += 50;
                key_press(KEY_SPACE, DOWN);
                msleep(50);
                key_press(KEY_SPACE, UP);
            }
        }
        msleep(100 - time_spent);
    }
}

int main()
{
    msleep(100);
    std::string keyboard_path;
    std::string mouse_path;

    initialization(Gvar_.macros_, keyboard_path, mouse_path);

    std::thread t_active_window(get_active_window);
    std::thread t_messages(Private::catch_message_return);
    std::thread t_mouse(read_mouse, mouse_path.c_str());
    std::thread t_keyboard(read_keyboard, keyboard_path.c_str());
    std::thread t_shift(shift_macro);

    printf("initialization finished!\n");

    ////////////////////////////////////////////
    std::string line;
    while (1)
    {
        std::getline(std::cin, line);

        if (line == "exit")
        {
            if (exit_fun())
            {
                // exit(1);
                return 0;
                break;
            }
        }
        else if (line == "close")
        {
            if (close_fun())
            {
                break;
            }
        }
        else
        {
            std::cout << "command not recognised!" << std::endl;
        }
    }
    ////////////////////////////////////////////

    t_mouse.join();
    t_keyboard.join();
    t_active_window.join();
    t_messages.join();
    t_shift.join();
    return 0;
}
