#include "compositor.h"

std::map<uint32_t, region> regions;
std::map<uint32_t, surface> surfaces;
void destroy_surface(struct wl_resource *resource)
{
    surfaces.erase(*(uint32_t *)(resource->data));
    free(resource->data);
}

void destroy_region(struct wl_resource *resource)
{
    regions.erase(*(uint32_t *)(resource->data));
    free(resource->data);
}

void create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    wl_resource *ret = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    if (!ret)
    {
        printf("surface creation failed!\n");
        return;
    }
    uint32_t *id_ptr = (uint32_t *)malloc(1 * sizeof(uint32_t));
    surfaces.emplace(id, surface(id, 0, 0));
    *id_ptr = id;
    wl_resource_set_implementation(ret, &wl_surface_interface, id_ptr, destroy_surface);
}

void create_region(struct wl_client *client,struct wl_resource *resource,uint32_t id)
{
    wl_resource *ret = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
    if (!ret)
    {
        printf("surface creation failed!\n");
        return;
    }
    uint32_t *id_ptr = (uint32_t *)malloc(1 * sizeof(uint32_t));
    regions.emplace(id, region(id, 0, 0));
    *id_ptr = id;
    wl_resource_set_implementation(ret, &wl_surface_interface, id_ptr, destroy_region);
}

void bind_compositor(struct wl_client *client,void *data,uint32_t version,uint32_t id) 
{
    struct wl_resource *resource = wl_resource_create(client, &wl_compositor_interface,version,id);
    if (!resource) 
    {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &wl_compositor_implementation,data, NULL);
}