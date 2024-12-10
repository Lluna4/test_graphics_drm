#pragma once
#include <wayland-server.h>
#include <stdio.h>
#include "surface.h"
#include "region.h"

void destroy_surface(struct wl_resource *resource);
void destroy_region(struct wl_resource *resource);
void create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id);
void create_region(struct wl_client *client,struct wl_resource *resource,uint32_t id);

static const struct wl_compositor_interface wl_compositor_implementation =
{
    .create_surface = create_surface,
    .create_region = create_region
};

void bind_compositor(struct wl_client *client,void *data,uint32_t version,uint32_t id);
