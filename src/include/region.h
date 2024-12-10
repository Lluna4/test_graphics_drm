#pragma once
#include <wayland-server.h>
#include <map>

struct region
{
    region(uint32_t id_, int x_, int y_)
    :id(id_), x(x_), y(y_)
    {}
    uint32_t id;
    int x;
    int y;
};