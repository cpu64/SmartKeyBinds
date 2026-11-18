#include "./config.hpp"
#include "./context.hpp"
#include "./input_listener.hpp"

#include <iostream>
#include <thread>

Context ctx;

int main()
{
    std::cout << "SmartKeyBinds controller starting\n";

    std::thread t_keyboard(listen_to_devices, Devices);

    std::cout << "SmartKeyBinds controller running\n";

    std::string line;
    while (1)
    {
        std::getline(std::cin, line);

        if (line == "exit")
        {
            return 0;
        }
        else if (line.empty())
        {
            break;
        }
        else
        {
            std::cout << "command not recognised!" << std::endl;
        }
    }

    t_keyboard.join();

    // close the fake input device

    std::cout << "SmartKeyBinds controller stopped\n";

    return 0;
}
