#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-client-protocol.h"
#include <wayland-client.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <sys/eventfd.h>
#include <thread>
#include <unordered_map>
#include <vector>

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
    std::thread m_worker;
    std::atomic<bool> m_running{false};

    int m_event_fd = -1;

    // Command queue
    std::mutex m_queue_mutex;
    std::deque<Command> m_commands;

    bool m_stopping = false;

    std::condition_variable m_startup_cv;
    bool m_startup_complete = false;
    bool m_startup_success = false;

    // Wayland connection (worker thread only)
    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_shm = nullptr;
    zwlr_layer_shell_v1 *m_layer_shell = nullptr;
    wp_viewporter *m_viewporter = nullptr;

    uint32_t m_layer_shell_version = 0;

    // Overlay storage (worker thread only)
    std::unordered_map<OverlayHandle, std::unique_ptr<Overlay>> m_overlays;

    // Handle allocation
    OverlayHandle m_next_handle = 1;

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
