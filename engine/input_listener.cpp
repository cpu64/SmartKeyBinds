#include "input_listener.hpp"
// #include "global_state.hpp"
#include "context.hpp"
#include "fake_input.hpp"
#include "macro_registry.hpp"
#include "thread_pool.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// extern GlobalState global_state;
extern Context ctx;

namespace
{

int open_input_device(const char *device_path)
{
    const int device_fd = open(device_path, O_RDONLY | O_NONBLOCK);

    if (device_fd == -1)
    {
        std::fprintf(stderr, "Failed to open input device '%s': %s\n", device_path, std::strerror(errno));

        return -1;
    }

    if (ioctl(device_fd, EVIOCGRAB, 1) == -1)
    {
        std::fprintf(stderr, "Failed to grab input device '%s': %s\n", device_path, std::strerror(errno));

        close(device_fd);
        return -1;
    }

    return device_fd;
}

void dispatch_key_event(const input_event &event)
{
    // global_state.keyboard_state[event.code] = event.value;

    // std::thread event_thread(*global_state.key_bindings[event.code], event);
    // std::cout << "Event type: " << event.type << " Event code: " << event.code << " Event value: " << event.value
    // << " Seconds: " << event.input_event_sec << " Microseconds: " << event.input_event_usec << std::endl;

    // std::thread event_thread(pass_event_on, event, fake_input_device);
    //
    // event_thread.detach();

    const auto &binding = MacroRegistry::instance().get(event.code);

    if (binding.function == nullptr)
    {
        // Default event handling
        pass_event_on(event);
        return;
    }

    if (binding.separate_thread)
    {
        ThreadPool::instance().enqueue(binding.function, event);
    }
    else
    {
        binding.function(event);
    }
}

} // namespace

void listen_to_devices(const std::vector<std::string> &device_paths)
{
    const int epoll_fd = epoll_create1(0);

    if (epoll_fd == -1)
    {
        std::perror("epoll_create1");
        return;
    }

    std::vector<int> device_fds;

    for (const auto &device_path : device_paths)
    {
        const int fd = open_input_device(device_path.c_str());

        if (fd == -1)
            continue;

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
        {
            std::fprintf(stderr, "Failed to register input device '%s': %s\n", device_path.c_str(),
                         std::strerror(errno));

            close(fd);
            continue;
        }

        device_fds.push_back(fd);
    }

    constexpr int MAX_EVENTS = 16;
    epoll_event events[MAX_EVENTS];

    input_event event{};

    while (true)
    {
        const int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (event_count == -1)
        {
            if (errno == EINTR)
                continue;

            std::perror("epoll_wait");
            break;
        }

        for (int i = 0; i < event_count; ++i)
        {
            const int fd = events[i].data.fd;

            if (events[i].events & (EPOLLERR | EPOLLHUP))
                continue;

            if (!(events[i].events & EPOLLIN))
                continue;

            while (true)
            {
                const ssize_t bytes_read = read(fd, &event, sizeof(event));

                if (bytes_read == -1)
                {
                    if (errno == EINTR)
                        continue;

                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;

                    std::fprintf(stderr, "Failed to read input device: %s\n", std::strerror(errno));

                    break;
                }

                if (bytes_read == 0)
                    break;

                if (bytes_read != sizeof(event))
                    continue;

                switch (event.type)
                {
                case EV_KEY:
                    ctx.real_keyboard_state[event.code] = event.value;
                    dispatch_key_event(event);
                    break;

                case EV_REL:
                    pass_event_on(event);
                    break;

                default:
                    break;
                }
            }
        }
    }

    for (const int fd : device_fds)
        close(fd);

    close(epoll_fd);
}
