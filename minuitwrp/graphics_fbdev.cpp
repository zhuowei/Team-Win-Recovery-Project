/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "minui.h"
#include "graphics.h"
#include <pixelflinger/pixelflinger.h>

static GRSurface* fbdev_init(minui_backend*);
static GRSurface* fbdev_flip(minui_backend*);
static void fbdev_blank(minui_backend*, bool);
static void fbdev_exit(minui_backend*);

static GRSurface gr_framebuffer[2];
static bool double_buffered;
static GRSurface* gr_draw = NULL;
static int displayed_buffer;

static fb_var_screeninfo vi;
static int fb_fd = -1;
static __u32 smem_len;

static minui_backend my_backend = {
    .init = fbdev_init,
    .flip = fbdev_flip,
    .blank = fbdev_blank,
    .exit = fbdev_exit,
};

minui_backend* open_fbdev() {
    return &my_backend;
}

static void fbdev_blank(minui_backend* backend __unused, bool blank)
{
#if defined(TW_NO_SCREEN_BLANK) && defined(TW_BRIGHTNESS_PATH) && defined(TW_MAX_BRIGHTNESS)
    int fd;
    char brightness[4];
    snprintf(brightness, 4, "%03d", TW_MAX_BRIGHTNESS/2);

    fd = open(TW_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        perror("cannot open LCD backlight");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);
#else
#ifndef TW_NO_SCREEN_BLANK
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
#endif
#endif
}

static void set_displayed_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffered) return;

    vi.yres_virtual = gr_framebuffer[0].height * 2;
    vi.yoffset = n * gr_framebuffer[0].height;
    vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
    displayed_buffer = n;
}

static GRSurface* fbdev_init(minui_backend* backend) {
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd == -1) {
        perror("cannot open fb0");
        return NULL;
    }

    fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    // We print this out for informational purposes only, but
    // throughout we assume that the framebuffer device uses an RGBX
    // pixel format.  This is the case for every development device I
    // have access to.  For some of those devices (eg, hammerhead aka
    // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
    // different format (XBGR) but actually produces the correct
    // results on the display when you write RGBX.
    //
    // If you have a device that actually *needs* another pixel format
    // (ie, BGRX, or 565), patches welcome...

    printf("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n",
           vi.bits_per_pixel,
           vi.red.offset, vi.red.length,
           vi.green.offset, vi.green.length,
           vi.blue.offset, vi.blue.length);

    void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return NULL;
    }

    memset(bits, 0, fi.smem_len);

#ifdef RECOVERY_RGB_565
    printf("Forcing pixel format: RGB_565\n");
    vi.blue.offset    = 0;
    vi.green.offset   = 5;
    vi.red.offset     = 11;
    vi.blue.length    = 5;
    vi.green.length   = 6;
    vi.red.length     = 5;
    vi.blue.msb_right = 0;
    vi.green.msb_right = 0;
    vi.red.msb_right = 0;
    vi.transp.offset  = 0;
    vi.transp.length  = 0;
    vi.bits_per_pixel = 16;
    vi.xres_virtual = fi.line_length / 2;
#endif

    gr_framebuffer[0].width = vi.xres;
    gr_framebuffer[0].height = vi.yres;
    gr_framebuffer[0].row_bytes = fi.line_length;
    gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
    gr_framebuffer[0].data = reinterpret_cast<uint8_t*>(bits);
    if (vi.bits_per_pixel == 16) {
        printf("setting GGL_PIXEL_FORMAT_RGB_565\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
    } else if (vi.red.offset == 8) {
        printf("setting GGL_PIXEL_FORMAT_BGRA_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_BGRA_8888;
    } else if (vi.red.offset == 0) {
        printf("setting GGL_PIXEL_FORMAT_RGBA_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBA_8888;
    } else if (vi.red.offset == 24) {
        printf("setting GGL_PIXEL_FORMAT_RGBX_8888\n");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
    } else {
        if (vi.red.length == 8) {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGBX_8888\n");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
        } else {
            printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565\n");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
        }
    }
    memset(gr_framebuffer[0].data, 0, gr_framebuffer[0].height * gr_framebuffer[0].row_bytes);

    // Drawing directly to the framebuffer takes about 5 times longer.
    // Instead, we will allocate some memory and draw to that, then
    // memcpy the data into the framebuffer later.
    gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
    if (!gr_draw) {
        perror("failed to allocate gr_draw");
        close(fd);
        munmap(bits, fi.smem_len);
        return NULL;
    }
    memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
    gr_draw->data = (unsigned char*) malloc(gr_draw->height * gr_draw->row_bytes);
    if (!gr_draw->data) {
        perror("failed to allocate in-memory surface");
        close(fd);
        free(gr_draw);
        munmap(bits, fi.smem_len);
        return NULL;
    }

    /* check if we can use double buffering */
    if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
        double_buffered = true;
        printf("double buffered\n");

        memcpy(gr_framebuffer+1, gr_framebuffer, sizeof(GRSurface));
        gr_framebuffer[1].data = gr_framebuffer[0].data +
            gr_framebuffer[0].height * gr_framebuffer[0].row_bytes;

    } else {
        double_buffered = false;
        printf("single buffered\n");
    }
#if defined(RECOVERY_BGRA)
    printf("RECOVERY_BGRA\n");
#endif
    memset(gr_draw->data, 0, gr_draw->height * gr_draw->row_bytes);
    fb_fd = fd;
    set_displayed_framebuffer(0);

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    fbdev_blank(backend, true);
    fbdev_blank(backend, false);

    smem_len = fi.smem_len;

    return gr_draw;
}

static GRSurface* fbdev_flip(minui_backend* backend __unused) {
    if (double_buffered) {
#if defined(RECOVERY_BGRA)
        // In case of BGRA, do some byte swapping
        unsigned int idx;
        unsigned char tmp;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
                idx += 4) {
            tmp = ucfb_vaddr[idx];
            ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
            ucfb_vaddr[idx + 2] = tmp;
        }
#endif
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[1-displayed_buffer].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
        set_displayed_framebuffer(1-displayed_buffer);
    } else {
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[0].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
    }
    return gr_draw;
}

static void fbdev_exit(minui_backend* backend __unused) {
    close(fb_fd);
    fb_fd = -1;

    if (gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
    munmap(gr_framebuffer[0].data, smem_len);
}
