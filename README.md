# Untitled wayland compositor
## Compilation
Prebuilt binaries are not ready yet so you have to compile it from source, this code uses cmake as a build system so you have to create a build directory and get to it `mkdir build && cd build` configure cmake `cmake ..` and then compile `cmake --build .` 

## Usage
Usage is quite limited right now but to open the server you just have to execute the executable from the compilation `./lunarland` (provisional name btw)

## Features
There's not a lot right now, just `wl_compositor` is complete, with basic implementations of `wl_surface` and `wl_region`, right now you can connect to it and create a surface or a region but not much more yet

## Planned development
The top priority thing right now is to finish all the core interfaces (`wl_surface, wl_region, wl_seat, wl_shm, etc...`)
Next would be combining it with a drm backend i [made](https://github.com/Lluna4/test_graphics_drm) and try to make a full screen window with it
And that would be all my """achievable""" plans for the near feature, i'm very happy with it working

