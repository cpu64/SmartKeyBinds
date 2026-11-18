#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "./viewporter-client-protocol.h"
#include "./wlr-layer-shell-client-protocol.h"
#include <wayland-client.h>

#include <cstddef>

#include <cstdint>
#include <vector>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sys/eventfd.h>
#include <thread>
#include <unordered_map>

namespace overlay
{

using OverlayHandle = uint64_t;

struct Image
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;

    // Premultiplied ARGB8888.
    std::vector<uint8_t> pixels;
};

class OverlayManager
{
  public:
    OverlayManager();
    ~OverlayManager();

    OverlayManager(const OverlayManager &) = delete;
    OverlayManager &operator=(const OverlayManager &) = delete;

    OverlayHandle create(Image image, uint32_t viewport_width, uint32_t viewport_height);

    void show(OverlayHandle handle, int x, int y);
    void hide(OverlayHandle handle);
    void destroy(OverlayHandle handle);

  private:
    enum class OverlayState
    {
        Creating,
        Ready,
        Destroying,
        Destroyed,
        Failed
    };

    enum class Visibility
    {
        Hidden,
        WaitingForConfigure,
        Visible
    };

    enum class CommandType
    {
        Create,
        Show,
        Hide,
        Destroy
    };

    struct Overlay
    {
        OverlayManager *manager = nullptr;
        OverlayHandle handle = 0;

        OverlayState state = OverlayState::Creating;
        Visibility visibility = Visibility::Hidden;

        wl_surface *surface = nullptr;
        zwlr_layer_surface_v1 *layer_surface = nullptr;
        wp_viewport *viewport = nullptr;
        wl_buffer *buffer = nullptr;

        int shm_fd = -1;
        void *pixels = nullptr;
        size_t buffer_size = 0;

        bool buffer_busy = false;
        bool surface_has_buffer = false;
        bool configured = false;
        bool closed = false;

        bool mapping_requested = false;

        uint32_t configured_width = 0;
        uint32_t configured_height = 0;
        uint32_t configure_count = 0;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;

        uint32_t viewport_width = 0;
        uint32_t viewport_height = 0;

        int x = 0;
        int y = 0;
    };

    struct Command
    {
        CommandType type;
        OverlayHandle handle;

        Image image;

        uint32_t viewport_width = 0;
        uint32_t viewport_height = 0;

        int x = 0;
        int y = 0;
    };

    // Worker thread
    std::thread worker_;
    std::atomic<bool> running_{false};

    int event_fd_ = -1;

    // Command queue
    std::mutex queue_mutex_;
    std::deque<Command> commands_;

    bool stopping_ = false;

    std::condition_variable startup_cv_;
    bool startup_complete_ = false;
    bool startup_success_ = false;

    // Wayland connection (worker thread only)
    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_compositor *compositor_ = nullptr;
    wl_shm *shm_ = nullptr;
    zwlr_layer_shell_v1 *layer_shell_ = nullptr;
    wp_viewporter *viewporter_ = nullptr;

    uint32_t layer_shell_version_ = 0;

    // Overlay storage (worker thread only)
    std::unordered_map<OverlayHandle, std::unique_ptr<Overlay>> overlays_;

    // Handle allocation
    OverlayHandle next_handle_ = 1;

    bool initialize_event_fd();
    void wake_worker();
    void process_commands();
    bool dispatch_wayland_events();
    void worker_loop();
    bool enqueue(Command command);
    void process_command(Command &command);

    bool initialize_wayland();
    void cleanup_wayland();

    static void on_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);

    static void on_global_remove(void *data, wl_registry *registry, uint32_t name);

    bool create_overlay(Overlay &overlay, Image image);
    void cleanup_overlay(Overlay &overlay);

    static void on_configure(void *data, zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t width,
                             uint32_t height);

    static void on_closed(void *data, zwlr_layer_surface_v1 *surface);

    static void on_buffer_release(void *data, wl_buffer *buffer);

    bool request_mapping(Overlay &overlay, int x, int y);
    bool attach_buffer(Overlay &overlay);
    void try_show_overlay(Overlay &overlay);
    void on_buffer_released(Overlay &overlay);
};

} // namespace overlay
