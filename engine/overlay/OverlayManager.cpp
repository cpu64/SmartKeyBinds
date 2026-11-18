#include "./OverlayManager.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/memfd.h>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace overlay
{

OverlayManager::OverlayManager()
{
    if (!initialize_event_fd())
        throw std::runtime_error("Failed to initialize eventfd");

    worker_ = std::thread(&OverlayManager::worker_loop, this);

    std::unique_lock lock(queue_mutex_);

    startup_cv_.wait(lock, [this] { return startup_complete_; });

    if (!startup_success_)
    {
        lock.unlock();

        if (worker_.joinable())
            worker_.join();

        close(event_fd_);
        event_fd_ = -1;

        throw std::runtime_error("Failed to initialize Wayland");
    }
}

OverlayManager::~OverlayManager()
{
    {
        std::lock_guard lock(queue_mutex_);
        stopping_ = true;
    }

    wake_worker();

    if (worker_.joinable())
        worker_.join();

    if (event_fd_ >= 0)
    {
        close(event_fd_);
        event_fd_ = -1;
    }
}

bool OverlayManager::enqueue(Command command)
{
    {
        std::lock_guard lock(queue_mutex_);

        if (stopping_)
            return false;

        commands_.push_back(std::move(command));
    }

    wake_worker();

    return true;
}

OverlayHandle OverlayManager::create(Image image, uint32_t viewport_width, uint32_t viewport_height)
{
    if (viewport_width == 0 || viewport_height == 0 || viewport_width > INT32_MAX || viewport_height > INT32_MAX)
    {
        return 0;
    }

    OverlayHandle handle;

    {
        std::lock_guard lock(queue_mutex_);

        if (stopping_ || next_handle_ == 0)
            return 0;

        handle = next_handle_++;

        Command command{};
        command.type = CommandType::Create;
        command.handle = handle;
        command.image = std::move(image);
        command.viewport_width = viewport_width;
        command.viewport_height = viewport_height;

        commands_.push_back(std::move(command));
    }

    wake_worker();

    return handle;
}

void OverlayManager::show(OverlayHandle handle, int x, int y)
{
    Command command{};
    command.type = CommandType::Show;
    command.handle = handle;
    command.x = x;
    command.y = y;

    enqueue(std::move(command));
}

void OverlayManager::hide(OverlayHandle handle)
{
    Command command{};
    command.type = CommandType::Hide;
    command.handle = handle;

    enqueue(std::move(command));
}

void OverlayManager::destroy(OverlayHandle handle)
{
    Command command{};
    command.type = CommandType::Destroy;
    command.handle = handle;

    enqueue(std::move(command));
}

void OverlayManager::on_global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                               uint32_t version)
{
    auto *manager = static_cast<OverlayManager *>(data);

    if (std::strcmp(interface, wl_compositor_interface.name) == 0)
    {
        if (!manager->compositor_)
        {
            manager->compositor_ = static_cast<wl_compositor *>(
                wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
        }
    }
    else if (std::strcmp(interface, wl_shm_interface.name) == 0)
    {
        if (!manager->shm_)
        {
            manager->shm_ = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        }
    }
    else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
    {
        if (!manager->layer_shell_)
        {
            const uint32_t supported_version =
                std::min(version, static_cast<uint32_t>(zwlr_layer_shell_v1_interface.version));

            manager->layer_shell_version_ = supported_version;

            manager->layer_shell_ = static_cast<zwlr_layer_shell_v1 *>(
                wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, supported_version));
        }
    }
    else if (std::strcmp(interface, wp_viewporter_interface.name) == 0)
    {
        if (!manager->viewporter_)
        {
            manager->viewporter_ =
                static_cast<wp_viewporter *>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
        }
    }
}

void OverlayManager::on_global_remove(void *, wl_registry *, uint32_t)
{
    // Global removal handling can be added later.
}

bool OverlayManager::initialize_wayland()
{
    display_ = wl_display_connect(nullptr);

    if (!display_)
        return false;

    registry_ = wl_display_get_registry(display_);

    if (!registry_)
        return false;

    static const wl_registry_listener registry_listener = {OverlayManager::on_global, OverlayManager::on_global_remove};

    if (wl_registry_add_listener(registry_, &registry_listener, this) < 0)
    {
        return false;
    }

    if (wl_display_roundtrip(display_) < 0)
        return false;

    if (!compositor_ || !shm_ || !layer_shell_ || !viewporter_)
        return false;

    return true;
}

void OverlayManager::cleanup_wayland()
{
    for (auto &[handle, overlay] : overlays_)
    {
        cleanup_overlay(*overlay);
        overlay->state = OverlayState::Destroyed;
    }

    overlays_.clear();

    if (layer_shell_)
    {
        zwlr_layer_shell_v1_destroy(layer_shell_);
        layer_shell_ = nullptr;
    }

    if (viewporter_)
    {
        wp_viewporter_destroy(viewporter_);
        viewporter_ = nullptr;
    }

    if (shm_)
    {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }

    if (compositor_)
    {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }

    if (registry_)
    {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }

    if (display_)
    {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

void OverlayManager::worker_loop()
{
    const bool initialized = initialize_wayland();

    running_ = initialized;

    {
        std::lock_guard lock(queue_mutex_);

        startup_success_ = initialized;
        startup_complete_ = true;

        if (!initialized)
            stopping_ = true;
    }

    startup_cv_.notify_one();

    if (!initialized)
    {
        cleanup_wayland();
        return;
    }

    while (true)
    {
        // Process all queued commands.
        process_commands();

        // Check whether shutdown was requested.
        {
            std::lock_guard lock(queue_mutex_);

            if (stopping_ && commands_.empty())
                break;
        }

        // Wait for either Wayland events or new commands.
        if (!dispatch_wayland_events())
            break;
    }

    cleanup_wayland();

    running_ = false;
}

void OverlayManager::process_commands()
{
    while (true)
    {
        Command command;

        {
            std::lock_guard lock(queue_mutex_);

            if (commands_.empty())
                break;

            command = std::move(commands_.front());
            commands_.pop_front();
        }

        process_command(command);
    }
}

void OverlayManager::process_command(Command &command)
{
    switch (command.type)
    {
    case CommandType::Create: {
        auto overlay = std::make_unique<Overlay>();
        overlay->handle = command.handle;
        overlay->manager = this;

        overlay->viewport_width = command.viewport_width;
        overlay->viewport_height = command.viewport_height;

        Overlay *ptr = overlay.get();

        overlays_.emplace(command.handle, std::move(overlay));

        if (!create_overlay(*ptr, std::move(command.image)))
        {
            cleanup_overlay(*ptr);
            ptr->state = OverlayState::Failed;
        }

        break;
    }

    case CommandType::Show: {
        auto it = overlays_.find(command.handle);

        if (it == overlays_.end())
            break;

        Overlay &overlay = *it->second;

        if (overlay.state != OverlayState::Ready || overlay.closed)
            break;

        // Remember the requested position.
        overlay.x = command.x;
        overlay.y = command.y;

        // A show request is already pending.
        if (overlay.visibility == Visibility::WaitingForConfigure)
        {
            if (!overlay.buffer_busy && !overlay.mapping_requested)
            {
                if (!request_mapping(overlay, overlay.x, overlay.y))
                    overlay.state = OverlayState::Failed;
            }

            break;
        }

        // Already visible: don't attach the same buffer twice.
        if (overlay.visibility == Visibility::Visible)
            break;

        // We need to wait for the previous buffer release
        // before requesting another mapping.
        if (overlay.buffer_busy)
        {
            overlay.visibility = Visibility::WaitingForConfigure;
            overlay.mapping_requested = false;
            break;
        }

        if (!request_mapping(overlay, command.x, command.y))
        {
            overlay.state = OverlayState::Failed;
            overlay.visibility = Visibility::Hidden;
        }

        break;
    }

    case CommandType::Hide: {
        auto it = overlays_.find(command.handle);

        if (it == overlays_.end())
            break;

        Overlay &overlay = *it->second;

        if (overlay.state != OverlayState::Ready || overlay.closed)
            break;

        // Cancel any pending show request.
        overlay.mapping_requested = false;
        overlay.visibility = Visibility::Hidden;

        // Nothing is currently attached.
        if (!overlay.surface_has_buffer)
            break;

        // Unmap the surface by attaching a null buffer.
        wl_surface_attach(overlay.surface, nullptr, 0, 0);
        wl_surface_commit(overlay.surface);

        overlay.surface_has_buffer = false;

        // The previous buffer may still be in use by the compositor.
        // buffer_busy will be cleared by on_buffer_release().

        if (wl_display_flush(display_) < 0 && errno != EAGAIN)
        {
            overlay.state = OverlayState::Failed;
            break;
        }

        break;
    }

    case CommandType::Destroy: {
        auto it = overlays_.find(command.handle);

        if (it == overlays_.end())
            break;

        Overlay &overlay = *it->second;

        if (overlay.state == OverlayState::Destroying || overlay.state == OverlayState::Destroyed)
            break;

        // Prevent further operations on this overlay.
        overlay.state = OverlayState::Destroying;
        overlay.visibility = Visibility::Hidden;
        overlay.mapping_requested = false;

        // If a buffer is still being accessed by the compositor,
        // defer destruction until its release callback.
        if (overlay.buffer_busy)
        {
            // Detach the buffer if it is currently attached.
            if (overlay.surface_has_buffer)
            {
                wl_surface_attach(overlay.surface, nullptr, 0, 0);
                wl_surface_commit(overlay.surface);

                overlay.surface_has_buffer = false;

                if (wl_display_flush(display_) < 0 && errno != EAGAIN)
                    running_ = false;
            }

            break;
        }

        cleanup_overlay(overlay);
        overlay.state = OverlayState::Destroyed;

        overlays_.erase(it);

        break;
    }
    }
}

void OverlayManager::on_configure(void *data, zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t width,
                                  uint32_t height)
{
    auto *overlay = static_cast<Overlay *>(data);

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    overlay->configured_width = width;
    overlay->configured_height = height;
    overlay->configured = true;
    overlay->mapping_requested = false;
    ++overlay->configure_count;

    if (overlay->state == OverlayState::Creating)
        overlay->state = OverlayState::Ready;

    if (overlay->manager)
        overlay->manager->try_show_overlay(*overlay);

    // The compositor may return dimensions different
    // from those requested. We'll handle that in Show.
}

void OverlayManager::on_closed(void *data, zwlr_layer_surface_v1 *)
{
    auto *overlay = static_cast<Overlay *>(data);

    overlay->closed = true;
    overlay->visibility = Visibility::Hidden;

    if (overlay->state != OverlayState::Destroying)
        overlay->state = OverlayState::Failed;
}

void OverlayManager::on_buffer_release(void *data, wl_buffer *)
{
    auto *overlay = static_cast<Overlay *>(data);

    if (overlay->manager)
        overlay->manager->on_buffer_released(*overlay);
}

bool OverlayManager::create_overlay(Overlay &overlay, Image image)
{
    if (overlay.viewport_width == 0 || overlay.viewport_height == 0)
        return false;

    if (image.width == 0 || image.height == 0)
        return false;

    if (image.width > INT32_MAX || image.height > INT32_MAX || image.stride > INT32_MAX)
    {
        return false;
    }

    const uint64_t minimum_stride = static_cast<uint64_t>(image.width) * 4;

    if (image.stride < minimum_stride)
        return false;

    const uint64_t required_size = static_cast<uint64_t>(image.stride) * image.height;

    if (required_size > INT32_MAX || required_size > image.pixels.size())
        return false;

    overlay.width = image.width;
    overlay.height = image.height;
    overlay.stride = image.stride;
    overlay.buffer_size = static_cast<size_t>(required_size);

    // Create shared memory file.
    overlay.shm_fd = memfd_create("smartkeybinds-overlay", MFD_CLOEXEC);

    if (overlay.shm_fd < 0)
        return false;

    if (ftruncate(overlay.shm_fd, static_cast<off_t>(overlay.buffer_size)) < 0)
        return false;

    // Map shared memory.
    overlay.pixels = mmap(nullptr, overlay.buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, overlay.shm_fd, 0);

    if (overlay.pixels == MAP_FAILED)
    {
        overlay.pixels = nullptr;
        return false;
    }

    // Copy image bytes.
    std::memcpy(overlay.pixels, image.pixels.data(), overlay.buffer_size);

    // Create SHM pool.
    wl_shm_pool *pool = wl_shm_create_pool(shm_, overlay.shm_fd, static_cast<int32_t>(overlay.buffer_size));

    if (!pool)
        return false;

    overlay.buffer =
        wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(overlay.width), static_cast<int32_t>(overlay.height),
                                  static_cast<int32_t>(overlay.stride), WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);

    if (!overlay.buffer)
        return false;

    static const wl_buffer_listener buffer_listener = {&OverlayManager::on_buffer_release};

    if (wl_buffer_add_listener(overlay.buffer, &buffer_listener, &overlay) < 0)
        return false;

    // Create surface.
    overlay.surface = wl_compositor_create_surface(compositor_);

    if (!overlay.surface)
        return false;

    wl_surface_set_buffer_scale(overlay.surface, 1);

    // Create viewport for fractional buffer-to-surface scaling.
    overlay.viewport = wp_viewporter_get_viewport(viewporter_, overlay.surface);

    if (!overlay.viewport)
        return false;

    // Create layer surface.
    overlay.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell_, overlay.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "smartkeybinds-overlay");

    if (!overlay.layer_surface)
        return false;

    static const zwlr_layer_surface_v1_listener layer_listener = {&OverlayManager::on_configure,
                                                                  &OverlayManager::on_closed};

    if (zwlr_layer_surface_v1_add_listener(overlay.layer_surface, &layer_listener, &overlay) < 0)
        return false;

    // Set initial properties.
    zwlr_layer_surface_v1_set_size(overlay.layer_surface, static_cast<int32_t>(overlay.viewport_width),
                                   static_cast<int32_t>(overlay.viewport_height));

    wp_viewport_set_destination(overlay.viewport, static_cast<int32_t>(overlay.viewport_width),
                                static_cast<int32_t>(overlay.viewport_height));

    zwlr_layer_surface_v1_set_anchor(overlay.layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

    zwlr_layer_surface_v1_set_exclusive_zone(overlay.layer_surface, 0);

    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay.layer_surface,
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    // Initial commit without a buffer requests configure.
    wl_surface_commit(overlay.surface);

    return wl_display_flush(display_) >= 0 || errno == EAGAIN;
}

void OverlayManager::cleanup_overlay(Overlay &overlay)
{
    if (overlay.layer_surface)
    {
        zwlr_layer_surface_v1_destroy(overlay.layer_surface);
        overlay.layer_surface = nullptr;
    }

    if (overlay.viewport)
    {
        wp_viewport_destroy(overlay.viewport);
        overlay.viewport = nullptr;
    }

    if (overlay.surface)
    {
        wl_surface_destroy(overlay.surface);
        overlay.surface = nullptr;
    }

    if (overlay.buffer)
    {
        wl_buffer_destroy(overlay.buffer);
        overlay.buffer = nullptr;
    }

    if (overlay.pixels)
    {
        munmap(overlay.pixels, overlay.buffer_size);
        overlay.pixels = nullptr;
    }

    if (overlay.shm_fd >= 0)
    {
        close(overlay.shm_fd);
        overlay.shm_fd = -1;
    }

    overlay.buffer_size = 0;
    overlay.buffer_busy = false;
    overlay.surface_has_buffer = false;
}

bool OverlayManager::initialize_event_fd()
{
    event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    return event_fd_ >= 0;
}

void OverlayManager::wake_worker()
{
    const uint64_t value = 1;

    ssize_t result;

    do
    {
        result = write(event_fd_, &value, sizeof(value));
    } while (result < 0 && errno == EINTR);

    // EAGAIN means the counter is already saturated,
    // so the worker is already guaranteed to be wakeable.
}

bool OverlayManager::dispatch_wayland_events()
{
    // Dispatch pending events before preparing to read.
    while (wl_display_prepare_read(display_) != 0)
    {
        if (wl_display_dispatch_pending(display_) < 0)
            return false;
    }

    bool want_write = false;

    // Flush outgoing Wayland requests.
    if (wl_display_flush(display_) < 0)
    {
        if (errno == EAGAIN)
        {
            want_write = true;
        }
        else
        {
            wl_display_cancel_read(display_);
            return false;
        }
    }

    pollfd fds[2]{};

    // Wayland display.
    fds[0].fd = wl_display_get_fd(display_);
    fds[0].events = POLLIN | (want_write ? POLLOUT : 0);

    // Command wakeup.
    fds[1].fd = event_fd_;
    fds[1].events = POLLIN;

    int result;

    do
    {
        result = poll(fds, 2, -1);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
    {
        wl_display_cancel_read(display_);
        return false;
    }

    // Check for Wayland connection errors.
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        wl_display_cancel_read(display_);
        return false;
    }

    // Read incoming Wayland events.
    if (fds[0].revents & POLLIN)
    {
        if (wl_display_read_events(display_) < 0)
            return false;
    }
    else
    {
        wl_display_cancel_read(display_);
    }

    // Flush pending outgoing requests if the socket
    // previously couldn't accept them.
    if (fds[0].revents & POLLOUT)
    {
        if (wl_display_flush(display_) < 0 && errno != EAGAIN)
            return false;
    }

    // Drain eventfd.
    if (fds[1].revents & POLLIN)
    {
        uint64_t value;

        while (true)
        {
            ssize_t bytes = read(event_fd_, &value, sizeof(value));

            if (bytes == sizeof(value))
                continue;

            if (bytes < 0 && errno == EINTR)
                continue;

            if (bytes < 0 && errno == EAGAIN)
                break;

            if (bytes < 0)
                return false;

            break;
        }
    }

    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
        return false;

    // Dispatch events received from Wayland.
    if (wl_display_dispatch_pending(display_) < 0)
        return false;

    return true;
}

bool OverlayManager::request_mapping(Overlay &overlay, int x, int y)
{
    if (!overlay.surface || !overlay.layer_surface || overlay.closed)
        return false;

    // We cannot remap while the previous buffer is still in use.
    if (overlay.buffer_busy || overlay.surface_has_buffer)
        return false;

    overlay.configured = false;
    overlay.mapping_requested = true;
    overlay.visibility = Visibility::WaitingForConfigure;

    overlay.x = x;
    overlay.y = y;

    // Reassert the layer after unmapping.
    if (layer_shell_version_ >= 2)
    {
        zwlr_layer_surface_v1_set_layer(overlay.layer_surface, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
    }

    zwlr_layer_surface_v1_set_size(overlay.layer_surface, static_cast<int32_t>(overlay.viewport_width),
                                   static_cast<int32_t>(overlay.viewport_height));

    wp_viewport_set_destination(overlay.viewport, static_cast<int32_t>(overlay.viewport_width),
                                static_cast<int32_t>(overlay.viewport_height));

    zwlr_layer_surface_v1_set_anchor(overlay.layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

    zwlr_layer_surface_v1_set_exclusive_zone(overlay.layer_surface, 0);

    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay.layer_surface,
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    zwlr_layer_surface_v1_set_margin(overlay.layer_surface,
                                     y, // top
                                     0, // right
                                     0, // bottom
                                     x  // left
    );

    // Commit without a buffer to request a fresh configure.
    wl_surface_commit(overlay.surface);

    return wl_display_flush(display_) >= 0 || errno == EAGAIN;
}

bool OverlayManager::attach_buffer(Overlay &overlay)
{
    if (!overlay.surface || !overlay.buffer || overlay.closed)
        return false;

    if (!overlay.configured || overlay.buffer_busy || overlay.surface_has_buffer)
    {
        return false;
    }

    wl_surface_attach(overlay.surface, overlay.buffer, 0, 0);

    wl_surface_damage(overlay.surface, 0, 0, static_cast<int32_t>(overlay.viewport_width),
                      static_cast<int32_t>(overlay.viewport_height));

    wl_surface_commit(overlay.surface);

    overlay.buffer_busy = true;
    overlay.surface_has_buffer = true;
    overlay.visibility = Visibility::Visible;

    overlay.mapping_requested = false;

    return wl_display_flush(display_) >= 0 || errno == EAGAIN;
}

void OverlayManager::try_show_overlay(Overlay &overlay)
{
    if (overlay.state != OverlayState::Ready)
        return;

    if (overlay.visibility != Visibility::WaitingForConfigure)
        return;

    if (!overlay.configured || overlay.buffer_busy)
        return;

    if (!attach_buffer(overlay))
    {
        overlay.state = OverlayState::Failed;
        overlay.visibility = Visibility::Hidden;
    }
}

void OverlayManager::on_buffer_released(Overlay &overlay)
{
    overlay.buffer_busy = false;

    // Destruction was requested while the compositor
    // was still using the buffer.
    if (overlay.state == OverlayState::Destroying)
    {
        cleanup_overlay(overlay);
        overlay.state = OverlayState::Destroyed;

        if (overlay.manager)
            overlay.manager->overlays_.erase(overlay.handle);

        return;
    }

    if (overlay.state != OverlayState::Ready)
        return;

    if (overlay.visibility != Visibility::WaitingForConfigure)
        return;

    // A show request may have been waiting for the old buffer.
    if (!overlay.mapping_requested)
    {
        if (!request_mapping(overlay, overlay.x, overlay.y))
        {
            overlay.state = OverlayState::Failed;
            overlay.visibility = Visibility::Hidden;
        }

        return;
    }

    try_show_overlay(overlay);
}
} // namespace overlay
