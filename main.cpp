#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>

struct colors
{
	int r, g, b;
};

struct colors random_color()
{
	int r, g, b;
	r = random()%256;
	g = random()%256;
	b = random()%256;
	struct colors a = {r, g, b};

	return a;
}

int main()
{
    using clock = std::chrono::system_clock;
    using ms = std::chrono::duration<double, std::milli>;
    srandom(time(NULL));
    int fd = open("/dev/dri/card1", O_RDWR | O_NONBLOCK);

    if (fd < 0)
    {
        printf("Fail\n");
        return -1;
    }
    printf("Success %i\n", fd);

    drmModeResPtr res =  drmModeGetResources(fd);

    if (res == NULL)
    {
        printf("Could not get resources\n");
        return -1;
    }


    drmModeConnectorPtr conn = NULL;
    for (int i = 0; i < res->count_connectors;i++)
    {
        conn = drmModeGetConnectorCurrent(fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED)
            break;
        printf("Connector is %s-%u height %i width %i\n", drmModeGetConnectorTypeName(conn->connector_type), conn->connector_type_id, conn->mmHeight, conn->mmWidth);
    }
    int height = 0;
    int width = 0;
    for (int i = 0; i < conn->count_modes; i++) 
    {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
        {
            height = conn->modes[i].vdisplay;
            width = conn->modes[i].hdisplay;
            break;
        }
    }
    drmModeModeInfoPtr resolution = 0;
    for (int i = 0; i < conn->count_modes; i++) 
    {
        resolution = &conn->modes[i];
        if (resolution->type & DRM_MODE_TYPE_PREFERRED)
        break;
    }
    printf("Refresh rate is %ihz\n", resolution->vrefresh);
    unsigned int handle = 0;
    unsigned int pitch = 0;
    unsigned long size = 0;
    unsigned int handle2 = 0;
    unsigned int pitch2 = 0;
    unsigned long size2 = 0;
    
    int err = drmModeCreateDumbBuffer(fd, width, height, 32, 0, &handle, &pitch, &size);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    err = drmModeCreateDumbBuffer(fd, width, height, 32, 0, &handle2, &pitch2, &size2);
    if (err < 0)
    {
        printf("Failed creating framebuffer 2 %i\n", err);
        return -1;
    }
    printf("Handle %i, pitch %i, size %u\n", handle, pitch, size);
    unsigned int buffer_id = 0;
    unsigned int buffer_id2 = 0;
    err = drmModeAddFB(fd, width, height, 24, 32, pitch, handle, &buffer_id);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    err = drmModeAddFB(fd, width, height, 24, 32, pitch2, handle2, &buffer_id2);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    
    drmModeEncoderPtr encoder =  drmModeGetEncoder(fd, conn->encoder_id);
    drmModeCrtcPtr crtc = drmModeGetCrtc(fd, encoder->crtc_id);
    unsigned long offset = 0;
    unsigned long offset2 = 0;
    drmModeMapDumbBuffer(fd, handle, &offset);
    drmModeMapDumbBuffer(fd, handle2, &offset2);
    char *frame_buffer1 = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (frame_buffer1 == MAP_FAILED)
    {
        printf("Mapping failed\n");
        return -1;
    }
    char *frame_buffer2 = (char *)mmap(0, size2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset2);
    if (frame_buffer2 == MAP_FAILED)
    {
        printf("Mapping failed\n");
        return -1;
    }
    drmSetMaster(fd);
   // drmModeSetCrtc(fd, crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
    drmModeSetCrtc(fd, crtc->crtc_id, buffer_id, 0, 0, &conn->connector_id, 1, resolution);
    int frames_to_write = 0;
    int main_fb = 1;
    int ev = 0;
    while (1)
    {
        if (frames_to_write == 0)
        {
            struct colors col = random_color();
            for (int x = 0; x < width; x++)
            {
                for (int y = 0; y < height; y++)
                {
                    int pxl_index = (x + width * y) * 4;
                    unsigned char r = col.b;
                    unsigned char g = col.g;
                    unsigned char b = col.r;
                    if (main_fb == 1)
                    {
                        frame_buffer2[pxl_index] = r;
                        pxl_index++;
                        frame_buffer2[pxl_index] = g;
                        pxl_index++;
                        frame_buffer2[pxl_index] = b;
                        pxl_index++;
                        frame_buffer2[pxl_index] = 255;
                        pxl_index++;
                    }
                    else
                    {
                        frame_buffer1[pxl_index] = r;
                        pxl_index++;
                        frame_buffer1[pxl_index] = g;
                        pxl_index++;
                        frame_buffer1[pxl_index] = b;
                        pxl_index++;
                        frame_buffer1[pxl_index] = 255;
                        pxl_index++;     
                    }
                }
            }
            if (main_fb == 1)
            {
                printf("page flip returned %i\n", drmModePageFlip(fd, crtc->crtc_id, buffer_id2, DRM_MODE_PAGE_FLIP_EVENT, &ev));
                main_fb = 2;
            }
            else if (main_fb == 2)
            {
                printf("page flip returned %i\n", drmModePageFlip(fd, crtc->crtc_id, buffer_id, DRM_MODE_PAGE_FLIP_EVENT, &ev));
                main_fb = 1;  
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    munmap(frame_buffer1, size);
    munmap(frame_buffer2, size);
    drmDropMaster(fd);
    return 0;
}
