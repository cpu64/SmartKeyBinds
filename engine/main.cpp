#include <iostream>
#include <thread>
#include <vector>

#include "./context.hpp"
#include "./input_listener.hpp"

Context ctx;

int main()
{
    std::cout << "SmartKeyBinds controller starting\n";

    std::thread t_keyboard(listen_to_devices,
                           std::vector<std::string>{"/dev/input/by-id/usb-Logitech_USB_Receiver-event-kbd",
                                                    "/dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse"});

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
