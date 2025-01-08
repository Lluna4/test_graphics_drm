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
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <filesystem>
#include <linux/input.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

int height = 0;
int width = 0; 
int drmfb = 0;
float lerp_t = 1.0f;

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

struct position
{
    float x, y;
};

GLuint program;
GLint cursor_location;
GLint screen_size_location;
position prev_mouse_abs = {0};
position target_mouse = {0};
position next_mouse = {0};

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

GLuint compile_shader(GLenum type, const char* source) 
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) 
    {
        GLint length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        char* info = new char[length];
        glGetShaderInfoLog(shader, length, NULL, info);
        printf("Shader compilation failed: %s\n", info);
        delete[] info;
        return 0;
    }
    return shader;
}

void mouse_read()
{
    int fd;
    if ((fd = open("/dev/input/mice", O_RDONLY)) < 0) 
    {
        printf("Cant open mouse\n");
        exit(1);
    }
    int epfd = epoll_create1(0);
    add_to_list(fd, epfd);
    int events_ready = 0;
    struct epoll_event events[1024];
    char buf[1024] = {0};
    signed char y_mov = 0;
    signed char x_mov = 0;
    unsigned char button_state = 0;
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
            button_state = buf[0];
            y_mov = buf[1];
            x_mov = buf[2];
            
            if ((button_state & 0b1) != 0)
                printf("Left mouse button pressed\n");
            if ((button_state & 0b10) != 0)
                printf("Right mouse button pressed\n");
            if ((button_state & 0b100) != 0)
                printf("Middle mouse button pressed\n");

            next_mouse.x = next_mouse.x + y_mov;
            next_mouse.y = next_mouse.y + (x_mov * -1);
            //printf("x %f y %f\n",mouse_position_x, mouse_position_y);
	        printf("x_mov %i y_mov %i\n", (int)y_mov, (int)x_mov);
            printf("next_mouse x %i next_mouse y %i\n", (int)next_mouse.x, (int)next_mouse.y);
            
        }
        memset(buf, 0, 4);
    }
}

void keyboard_read()
{
    int fd = -2;
    for (const auto & entry : std::filesystem::directory_iterator("/dev/input/by-id"))
    {
        if (strstr(entry.path().filename().c_str(), "Keyboard") != NULL && strstr(entry.path().filename().c_str(), "kbd") != NULL)
        {
            fd = open(entry.path().c_str(), O_RDONLY);
            break;
        }
    }
    if (fd == -2)
    {
        for (const auto & entry : std::filesystem::directory_iterator("/dev/input/by-path"))
        {
            if (strstr(entry.path().filename().c_str(), "kbd") != NULL)
            {
                fd = open(entry.path().c_str(), O_RDONLY);
                break;
            }
        }
    }
    if (fd < 0)
    {
        printf("Opening a keyboard failed!\n");
        return;
    }
    input_event ev = {0};
    int index = 0;
    int status = 0;
    while (1)
    {
        status = read(fd, &ev, sizeof(struct input_event));
        if (ev.type == EV_KEY) 
        {
            if (ev.value == 1) 
            {
                printf("Key %d pressed\n", ev.code);
            } 
            else if (ev.value == 0) 
            {
                printf("Key %d released\n", ev.code);
            }
        }
    }
}

float lerp(float v0, float v1, float t) 
{
    return (1 - t) * v0 + t * v1;
}

int main(int argc, char *argv[])
{
    EGLint major, minor, n;
    struct gbm_bo *bo;
    uint32_t width, height, stride, handle;
	int target_refresh = 60;
    if (argc > 1)
    {
		if (*argv[1] == '-' && argv[1][1] == 'r')
		{
				if (argc > 2)
				{
                    target_refresh = atoi(argv[2]);
                    if (target_refresh == 0)
                            target_refresh = 60;
				}
				else
				{
					printf("invalid command! You must leave an space between -r and the number, for example (-r 144)\n");
				}
		}
	}
	printf("Target refresh is %i\n", target_refresh);
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

    std::ifstream t("vertex.glsl");
    std::stringstream vert_shader;
    vert_shader << t.rdbuf();

    std::ifstream t2("fragment.glsl");
    std::stringstream frag_shader;
    frag_shader << t2.rdbuf();
    
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vert_shader.str().c_str());
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, frag_shader.str().c_str());

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    

    cursor_location = glGetAttribLocation(program, "a_Position");
    screen_size_location = glGetUniformLocation(program, "u_ScreenSize");

    
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    next_mouse.x = width/2;
    next_mouse.y = height/2;
    prev_mouse_abs = next_mouse;
    target_mouse = next_mouse;
    std::thread m_read(mouse_read);
    m_read.detach();
    std::thread kbd_th(keyboard_read);
    kbd_th.detach();

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
    GLfloat r = 0.0f, g = 0.0f, b = 0.0f;
    while (1)
    {
        struct gbm_bo *next_bo;
        printf("Frame: %i\n", f_number);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(r, g, b, 1.0f);
        glUseProgram(program);
        glUniform2f(screen_size_location, (float)width, (float)height);
        float new_x = lerp(prev_mouse_abs.x, target_mouse.x, lerp_t);
        float new_y = lerp(prev_mouse_abs.y, target_mouse.y, lerp_t);
        float vertices[] = 
        {
            new_x, new_y,
            new_x + 10, new_y,
            new_x, new_y + 10,
            new_x + 10, new_y + 10
        };
        // Update the vertex buffer
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(cursor_location);
        glVertexAttribPointer(cursor_location, 2, GL_FLOAT, GL_FALSE, 0, 0);
        
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(gl_display, gl_surface);
		next_bo = gbm_surface_lock_front_buffer(gbm_s);
		fb = drm_fb_get_from_bo(next_bo);
        drmModePageFlip(drmfb, crtc->crtc_id, fb->fb_id, DRM_MODE_PAGE_FLIP_EVENT, &ev);
        wait_ep(epfd, drmfb);
		
		gbm_surface_release_buffer(gbm_s, bo);
		bo = next_bo;
        f_number++;
        lerp_t += 0.5f;
        if (lerp_t >= 1.0f)
        {
            prev_mouse_abs = target_mouse;
            target_mouse = next_mouse;
            lerp_t = 0.0f;
        }
    }
}
