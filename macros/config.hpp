#pragma once
#include <string>
#include <vector>

// constexpr std::array<std::string_view, 2> DEVICES = {"/dev/input/by-id/usb-Logitech_USB_Receiver-event-kbd",
//                                                      "/dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse"};

const std::vector<std::string> Devices = {"/dev/input/by-id/usb-Logitech_USB_Receiver-event-kbd",
                                          "/dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse"};
