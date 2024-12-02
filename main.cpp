#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>
#include <sys/epoll.h>
#include <unistd.h>
float mouse_position_y = 0;
float mouse_position_x = 0;

float fb1_pos_x = 0;
float fb1_pos_y = 0;
float fb2_pos_x = 0;
float fb2_pos_y = 0;
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

void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
    printf("%u %u %u\n", sequence, tv_sec, tv_usec);
}

void add_to_list(int fd, int epfd)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

void mouse_read()
{
    int fd;
    if ((fd = open("/dev/input/mice", O_RDONLY)) < 0) 
    {
        perror("evdev open");
        exit(1);
    }
    int epfd = epoll_create1(0);
    add_to_list(fd, epfd);
    int events_ready = 0;
    struct epoll_event events[1024];
    char buf[1024] = {0};
    signed char y_mov = 0;
    signed char x_mov = 0;
    while(1) 
    {
        int status = 0;
        events_ready = epoll_wait(epfd, events, 1024, -1);
        for (int i = 0; i < events_ready;i++)
        {
            status = read(fd, buf, 4);
            if (status <= 0)
            {
                printf("Mouse handler crashed! Read failed\n");
                return;
            }
            y_mov = buf[1];
            x_mov = buf[2];
            mouse_position_y += x_mov * -1;
            mouse_position_x += y_mov;
            //printf("x %f y %f\n",mouse_position_x, mouse_position_y);
	        //printf("x_mov %i y_mov %i\n", (int)y_mov, (int)x_mov);
            
        }
        memset(buf, 0, 4);
    }
}

void delete_cursor(int m_pos_x, int m_pos_y, int width ,char *framebuffer)
{
    for (int x = m_pos_x; x < m_pos_x + 5; x++)
    {
        for (int y = m_pos_y; y < m_pos_y + 5; y++)
        {
            int pxl_index = (x + width * y) * 4;

            framebuffer[pxl_index] = 0;
            pxl_index++;
            framebuffer[pxl_index] = 0;
            pxl_index++;
            framebuffer[pxl_index] = 0;
            pxl_index++;
            framebuffer[pxl_index] = 255;
            pxl_index++;
        }
    }
}

void draw_cursor(int m_pos_x, int m_pos_y, int width ,char *framebuffer)
{
    for (int x = m_pos_x; x < m_pos_x + 5; x++)
    {
        for (int y = m_pos_y; y < m_pos_y + 5; y++)
        {
            int pxl_index = (x + width * y) * 4;

            framebuffer[pxl_index] = 255;
            pxl_index++;
            framebuffer[pxl_index] = 255;
            pxl_index++;
            framebuffer[pxl_index] = 255;
            pxl_index++;
            framebuffer[pxl_index] = 255;
            pxl_index++;
        }
    }
}

void wait_ep(int epfd, int fd)
{
    struct epoll_event events[1024];
    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };
    epoll_wait(epfd, events, 1024, -1);
    drmHandleEvent(fd, &evctx);
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
    mouse_position_x = width/2;
    fb1_pos_x = mouse_position_x;
    fb2_pos_x = mouse_position_x;
    mouse_position_y = height/2;
    fb1_pos_y = mouse_position_y;
    fb2_pos_y = mouse_position_y;
    drmModeModeInfoPtr resolution = 0;
    for (int i = 0; i < conn->count_modes; i++) 
    {
        resolution = &conn->modes[i];
        if (resolution->type & DRM_MODE_TYPE_PREFERRED)
        break;
    }
    printf("Refresh rate is %ihz\n", resolution->vrefresh);
    drmModeFB *FB1 = (drmModeFB *)malloc(sizeof(drmModeFB));
    drmModeFB *FB2 = (drmModeFB *)malloc(sizeof(drmModeFB));
    unsigned long size = 0;
    unsigned int handle2 = 0;
    unsigned int pitch2 = 0;
    unsigned long size2 = 0;
    
    int err = drmModeCreateDumbBuffer(fd, width, height, 32, 0, &FB1->handle, &FB1->pitch, &size);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    err = drmModeCreateDumbBuffer(fd, width, height, 32, 0, &FB2->handle, &FB2->pitch, &size2);
    if (err < 0)
    {
        printf("Failed creating framebuffer 2 %i\n", err);
        return -1;
    }
    printf("Handle %i, pitch %i, size %u\n", FB1->handle, FB1->pitch, size);
    unsigned int buffer_id = 0;
    unsigned int buffer_id2 = 0;
    err = drmModeAddFB(fd, width, height, 24, 32, FB1->pitch, FB1->handle, &FB1->fb_id);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    err = drmModeAddFB(fd, width, height, 24, 32, FB2->pitch, FB2->handle, &FB2->fb_id);
    if (err < 0)
    {
        printf("Failed creating framebuffer %i\n", err);
        return -1;
    }
    
    drmModeEncoderPtr encoder =  drmModeGetEncoder(fd, conn->encoder_id);
    drmModeCrtcPtr crtc = drmModeGetCrtc(fd, encoder->crtc_id);
    unsigned long offset = 0;
    unsigned long offset2 = 0;
    drmModeMapDumbBuffer(fd, FB1->handle, &offset);
    drmModeMapDumbBuffer(fd, FB2->handle, &offset2);
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
    if (drmSetMaster(fd)!= 0)
    {
        printf("Setting to master failed\n");
        return 0;
    }
    //drmModeSetCrtc(fd, crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
    drmModeSetCrtc(fd, crtc->crtc_id, FB1->fb_id, 0, 0, &conn->connector_id, 1, resolution);
    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };
    int frames_to_write = 0;
    int main_fb = 1;
    int ev = 1;
    std::thread mouse_th(mouse_read);
    mouse_th.detach();
    int epfd = epoll_create1(0);
    add_to_list(fd, epfd);
    int events_ready = 0;
    struct epoll_event events[1024];
    int f_number = 0;
    while (1)
    {
        printf("Frame: %i\n", f_number);
        if (mouse_position_x < 0)
            mouse_position_x = 0;
        else if (mouse_position_x > width - 5)
            mouse_position_x = width - 5;
        if (mouse_position_y < 0)
            mouse_position_y = 0;
        else if (mouse_position_y > height - 5)
            mouse_position_y = height - 5;
        ev = 0;
        
        if (main_fb == 1)
        {
            delete_cursor(fb2_pos_x, fb2_pos_y, width, frame_buffer2);
            draw_cursor(mouse_position_x, mouse_position_y, width, frame_buffer2);
            fb2_pos_x = mouse_position_x;
            fb2_pos_y = mouse_position_y;
            if (f_number > 0)
                wait_ep(epfd, fd);
            printf("page flip returned %i\n", drmModePageFlip(fd, crtc->crtc_id, FB2->fb_id, DRM_MODE_PAGE_FLIP_EVENT, &ev));
            main_fb = 2;

            
        }
        else if (main_fb == 2)
        {
            delete_cursor(fb1_pos_x, fb1_pos_y, width, frame_buffer1);
            draw_cursor(mouse_position_x, mouse_position_y, width, frame_buffer1);
            fb1_pos_x = mouse_position_x;
            fb1_pos_y = mouse_position_y;
            wait_ep(epfd, fd);
            printf("page flip returned %i\n", drmModePageFlip(fd, crtc->crtc_id, FB1->fb_id, DRM_MODE_PAGE_FLIP_EVENT, &ev));
            main_fb = 1;
        } 
        f_number++;
    }
    drmModeFreeCrtc(crtc);
    munmap(frame_buffer1, size);
    munmap(frame_buffer2, size);
    drmDropMaster(fd);
    return 0;
}
