#include "include/compositor.h"

int main()
{
    wl_display *disp = wl_display_create();
    if (!disp)
    {
        printf("Failed to start wayland display\n");
        return -1;
    }
    const char *sock = wl_display_add_socket_auto(disp);
    if (!sock)
    {
        printf("Failed to bind to socket\n");
        return -1;
    }
    wl_global *global_compositor = wl_global_create(disp, &wl_compositor_interface, 4, NULL, bind_compositor);
    if (!global_compositor)
    {
        printf("Failed to create compositor global\n");
        return -1;
    }
    printf("Wayland display is running on %s!\n", sock);

    wl_display_run(disp);
    wl_display_destroy(disp);
    return 0;
}