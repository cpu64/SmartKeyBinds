#include "./input_listener.hpp"

#include "./context.hpp"
#include "./fake_input.hpp"
#include "./macro_registry.hpp"
#include "./thread_pool.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

extern Context ctx;

namespace
{

int open_input_device(const char *device_path)
{
    const int DeviceFd = open(device_path, O_RDONLY | O_NONBLOCK);

    if (DeviceFd == -1)
    {
        std::fprintf(stderr, "Failed to open input device '%s': %s\n", device_path, std::strerror(errno));

        return -1;
    }

    if (ioctl(DeviceFd, EVIOCGRAB, 1) == -1)
    {
        std::fprintf(stderr, "Failed to grab input device '%s': %s\n", device_path, std::strerror(errno));

        close(DeviceFd);
        return -1;
    }

    return DeviceFd;
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

void listen_to_devices(std::vector<std::string> device_paths)
{
    const int EpollFd = epoll_create1(0);

    if (EpollFd == -1)
    {
        std::perror("epoll_create1");
        return;
    }

    std::vector<int> device_fds;

    for (const auto &device_path : device_paths)
    {
        const int Fd = open_input_device(device_path.data());

        if (Fd == -1)
            continue;

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = Fd;

        if (epoll_ctl(EpollFd, EPOLL_CTL_ADD, Fd, &event) == -1)
        {
            std::fprintf(stderr, "Failed to register input device '%s': %s\n", device_path.data(),
                         std::strerror(errno));

            close(Fd);
            continue;
        }

        device_fds.push_back(Fd);
    }

    constexpr int MaxEvents = 16;
    epoll_event events[MaxEvents];

    input_event event{};

    while (true)
    {
        const int EventCount = epoll_wait(EpollFd, events, MaxEvents, -1);

        if (EventCount == -1)
        {
            if (errno == EINTR)
                continue;

            std::perror("epoll_wait");
            break;
        }

        for (int i = 0; i < EventCount; ++i)
        {
            const int Fd = events[i].data.fd;

            if (events[i].events & (EPOLLERR | EPOLLHUP))
                continue;

            if (!(events[i].events & EPOLLIN))
                continue;

            while (true)
            {
                const ssize_t BytesRead = read(Fd, &event, sizeof(event));

                if (BytesRead == -1)
                {
                    if (errno == EINTR)
                        continue;

                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;

                    std::fprintf(stderr, "Failed to read input device: %s\n", std::strerror(errno));

                    break;
                }

                if (BytesRead == 0)
                    break;

                if (BytesRead != sizeof(event))
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

    for (const int Fd : device_fds)
        close(Fd);

    close(EpollFd);
}
