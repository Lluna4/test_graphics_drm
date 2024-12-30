#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

int height = 0;
int width = 0; 
int drmfb = 0;

static const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

static const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
};

struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};

void add_to_list(int fd, int epfd)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
    printf("%u %u %u\n", sequence, tv_sec, tv_usec);
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

static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = (drm_fb *)data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	if (fb->fb_id)
		drmModeRmFB(drmfb, fb->fb_id);

	free(fb);
}

static struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
	struct drm_fb *fb = (drm_fb *)gbm_bo_get_user_data(bo);
	uint32_t width, height, stride, handle;
	int ret;

	if (fb)
		return fb;

	fb = (drm_fb *)calloc(1, sizeof *fb);
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	stride = gbm_bo_get_stride(bo);
	handle = gbm_bo_get_handle(bo).u32;

	ret = drmModeAddFB(drmfb, width, height, 24, 32, stride, handle, &fb->fb_id);
	if (ret) {
		printf("failed to create fb: %s\n", strerror(errno));
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;
}

int main()
{
    EGLint major, minor, n;
    struct gbm_bo *bo;
    uint32_t width, height, stride, handle;
    int target_refresh = 165;
    struct drm_fb *fb;
    srand((unsigned int)time(NULL));
	drmfb = open("/dev/dri/card0", O_RDWR | O_NONBLOCK);

	if (drmfb <= 0)
	{
		printf("Opening device failed\n");
		return -1;
	}
	struct gbm_device *gbm_dev = gbm_create_device(drmfb);
    drmModeResPtr res =  drmModeGetResources(drmfb);

    if (res == NULL)
    {
        printf("Could not get resources\n");
        return -1;
    }


    drmModeConnectorPtr conn = NULL;
    for (int i = 0; i < res->count_connectors;i++)
    {
        conn = drmModeGetConnectorCurrent(drmfb, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED)
            break;
    }
    printf("Connector is %s-%u height %i width %i\n", drmModeGetConnectorTypeName(conn->connector_type), conn->connector_type_id, conn->mmHeight, conn->mmWidth);
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
        if (resolution->vrefresh == target_refresh)
            break;
    }
    printf("Refresh rate is %ihz\n", resolution->vrefresh);
	struct gbm_surface *gbm_s = gbm_surface_create(
	    gbm_dev, 
	    width, 
	    height, 
	    GBM_FORMAT_ABGR8888,0
	);
    if (!gbm_s)
    {
        printf("Failed to create gbm surface %s\n", strerror(errno));
    }
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
	get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay gl_display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm_dev, NULL);
	if (!eglInitialize(gl_display, &major, &minor)) 
    {
		printf("failed to initialize egl\n");
		return -1;
	}
	printf("Using display %p with EGL version %d.%d\n",gl_display, major, minor);
	if (!eglBindAPI(EGL_OPENGL_ES_API)) 
    {
		printf("failed to bind api EGL_OPENGL_ES_API\n");
		return -1;
	}
    EGLConfig gl_config;
	if (!eglChooseConfig(gl_display, config_attribs, &gl_config, 1, &n) || n != 1) 
    {
		printf("failed to choose config: %d\n", n);
		return -1;
	}
	EGLContext gl_context = eglCreateContext(gl_display, gl_config,EGL_NO_CONTEXT, context_attribs);
	if (gl_context == NULL) 
    {
		printf("failed to create egl context\n");
		return -1;
	}

	EGLSurface gl_surface = eglCreateWindowSurface(gl_display, gl_config, gbm_s, NULL);
	if (gl_surface == EGL_NO_SURFACE) 
    {
		printf("failed to create egl surface\n");
		return -1;
	}
    eglMakeCurrent(gl_display, gl_surface, gl_surface, gl_context);

    glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(gl_display, gl_surface);

    bo = gbm_surface_lock_front_buffer(gbm_s);
    fb = drm_fb_get_from_bo(bo);

    drmModeEncoderPtr encoder =  drmModeGetEncoder(drmfb, conn->encoder_id);
    drmModeCrtcPtr crtc = drmModeGetCrtc(drmfb, encoder->crtc_id);

	drmModeSetCrtc(drmfb, crtc->crtc_id, fb->fb_id, 0, 0, &conn->connector_id, 1, resolution);
    int f_number = 0;
    int epfd = epoll_create1(0);
    int ev = 1;
    add_to_list(drmfb, epfd);
    GLfloat r = 0.2f, g = 0.3f, b = 0.4f;

    while (1)
    {
        struct gbm_bo *next_bo;
        printf("Frame: %i\n", f_number);
        if (f_number%target_refresh == 0 || f_number == 0)
        {
            r = ((float)rand()/(float)(RAND_MAX)) * 1.0;
            g = ((float)rand()/(float)(RAND_MAX)) * 1.0;
            b = ((float)rand()/(float)(RAND_MAX)) * 1.0;
        }
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(r, g, b, 1.0f);
        eglSwapBuffers(gl_display, gl_surface);
		next_bo = gbm_surface_lock_front_buffer(gbm_s);
		fb = drm_fb_get_from_bo(next_bo);
        drmModePageFlip(drmfb, crtc->crtc_id, fb->fb_id, DRM_MODE_PAGE_FLIP_EVENT, &ev);
        wait_ep(epfd, drmfb);
		
		gbm_surface_release_buffer(gbm_s, bo);
		bo = next_bo;
        f_number++;
    }
}