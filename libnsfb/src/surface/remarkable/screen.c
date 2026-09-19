#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <linux/fb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

#include "screen.h"
#include "mxcfb.h"
#include "log.h"

#define UNUSED(x) ((x) = (x))

static void *fb_async_redraw(void *context)
{
	struct timespec millisecond_sleep;
	millisecond_sleep.tv_nsec = 200000000;
	millisecond_sleep.tv_sec = 0;

	fb_state_t *state = (fb_state_t *)context;
	while (state->redraw_active) {
		if (pthread_mutex_lock(&state->fb_mutex) != 0) {
			ERROR_LOG(
				"fb_async_redraw: Redrawing thread could not lock mutex.");
		}

		if (state->next_update_x1 > state->next_update_x0 && 
		    state->next_update_y1 > state->next_update_y0) {
			DEBUG_LOG("fb_async_redraw: Processing update region (%d,%d,%d,%d)",
				state->next_update_x0, state->next_update_y0,
				state->next_update_x1, state->next_update_y1);
			struct mxcfb_update_data update_data;
			struct mxcfb_rect update_rect;
			
			/* Transform coordinates for landscape orientation */
			/* In landscape mode, logical coordinates need to be rotated 90° CCW */
			/* Physical framebuffer is always 1404x1872 (portrait) */
			/* Logical (x, y) -> Physical (y, 1872 - x) */
			if (state->orientation == SCREEN_ORIENTATION_LANDSCAPE) {
				int phys_x0 = state->next_update_y0;
				int phys_y0 = 1872 - state->next_update_x1;
				int phys_x1 = state->next_update_y1;
				int phys_y1 = 1872 - state->next_update_x0;
				
				update_rect.left = phys_x0;
				update_rect.top = phys_y0;
				update_rect.width = phys_x1 - phys_x0;
				update_rect.height = phys_y1 - phys_y0;
				
				DEBUG_LOG("fb_async_redraw: Transformed to physical coords: left=%d, top=%d, width=%d, height=%d",
					update_rect.left, update_rect.top, update_rect.width, update_rect.height);
			} else {
				update_rect.left = state->next_update_x0;
				update_rect.width = (state->next_update_x1 -
						     state->next_update_x0);
				update_rect.top = state->next_update_y0;
				update_rect.height = (state->next_update_y1 -
						      state->next_update_y0);
			}

			update_data.update_region = update_rect;
			update_data.waveform_mode = DEFAULT_WAVEFORM_MODE;
			update_data.update_mode = UPDATE_MODE_PARTIAL;
			update_data.update_marker = 0;
			update_data.dither_mode = DEFAULT_EPDC_FLAG;
			update_data.temp = DEFAULT_TEMP;
			update_data.flags = 0;

			if (ioctl(state->fb, MXCFB_SEND_UPDATE, &update_data) ==
			    -1) {
				ERROR_LOG(
					"fb_async_redraw: error executing MXCFB_SEND_UPDATE! errno=%d (%s)",
					errno, strerror(errno));
			} else {
				DEBUG_LOG("fb_async_redraw: MXCFB_SEND_UPDATE succeeded");
			}
			TRACE_LOG(
				"fb_async_redraw: Sent MXCFB_SEND_UPDATE ioctl for region: left=%d, width=%d, top=%d, height=%d",
				update_rect.left,
				update_rect.width,
				update_rect.top,
				update_rect.height);

			state->next_update_x0 = INT_MAX;
			state->next_update_x1 = INT_MIN;
			state->next_update_y0 = INT_MAX;
			state->next_update_y1 = INT_MIN;
		}

		pthread_mutex_unlock(&state->fb_mutex);
		nanosleep(&millisecond_sleep, &millisecond_sleep);
	}
	return 0;
}

int fb_claim_region(fb_state_t *fb_state, nsfb_bbox_t *box)
{
	UNUSED(box);
	if (pthread_mutex_lock(&fb_state->fb_mutex) != 0) {
		ERROR_LOG("fb_claim_region: Could not lock update mutex.");
		return -1;
	}
	return 0;
}

int fb_update_region(fb_state_t *fb_state, nsfb_bbox_t *box)
{
	UNUSED(box);
	DEBUG_LOG("fb_update_region: box=(%d,%d,%d,%d)", box->x0, box->y0, box->x1, box->y1);
	fb_state->next_update_x0 = MIN(fb_state->next_update_x0, box->x0);
	fb_state->next_update_x1 = MAX(fb_state->next_update_x1, box->x1);
	fb_state->next_update_y0 = MIN(fb_state->next_update_y0, box->y0);
	fb_state->next_update_y1 = MAX(fb_state->next_update_y1, box->y1);
	DEBUG_LOG("fb_update_region: next_update=(%d,%d,%d,%d)", 
		fb_state->next_update_x0, fb_state->next_update_y0,
		fb_state->next_update_x1, fb_state->next_update_y1);
	if (pthread_mutex_unlock(&fb_state->fb_mutex) != 0) {
		ERROR_LOG("fb_update_region: could not unlock update mutex.");
	}
	return 0;
}

int fb_initialize(fb_state_t *fb_state, screen_orientation_t orientation)
{
	int fb = open(FRAMEBUFFER_FILE, O_RDWR);
	if (fb == -1) {
		ERROR_LOG("fb_initialize: could not open framebuffer");
		return -1;
	}
	fb_state->fb = fb;
	DEBUG_LOG("fb_initialize: Framebuffer %s opened", FRAMEBUFFER_FILE);

	struct fb_fix_screeninfo f_screen_info;
	if (ioctl(fb_state->fb, FBIOGET_FSCREENINFO, &f_screen_info) != 0) {
		ERROR_LOG("fb_initialize: could not FBIOGET_FSCREENFINO");
		return -1;
	}
	struct fb_var_screeninfo v_screen_info;
	if (ioctl(fb_state->fb, FBIOGET_VSCREENINFO, &v_screen_info) != 0) {
		ERROR_LOG("fb_initialize: could not FBIOGET_VSCREENFINO");
		return -1;
	}
	
	fb_state->orientation = orientation;
	
	/* Store the physical framebuffer dimensions */
	int physical_width = v_screen_info.xres;
	int physical_height = v_screen_info.yres;
	
	/* Apply orientation - swap width/height for landscape mode */
	if (orientation == SCREEN_ORIENTATION_LANDSCAPE) {
		fb_state->scrinfo.width = physical_height;
		fb_state->scrinfo.height = physical_width;
		DEBUG_LOG("fb_initialize: Landscape mode - swapped dimensions");
	} else {
		fb_state->scrinfo.width = physical_width;
		fb_state->scrinfo.height = physical_height;
		DEBUG_LOG("fb_initialize: Portrait mode - native dimensions");
	}
	
	/* Line length stays at physical value for memory layout */
	fb_state->scrinfo.linelen = f_screen_info.line_length;
	fb_state->scrinfo.bpp = v_screen_info.bits_per_pixel;
	fb_state->fb_size = v_screen_info.yres_virtual *
			    f_screen_info.line_length;
	DEBUG_LOG("fb_initialize: Screeninfo loaded: width=%d, height=%d, orientation=%s",
		  fb_state->scrinfo.width,
		  fb_state->scrinfo.height,
		  orientation == SCREEN_ORIENTATION_LANDSCAPE ? "landscape" : "portrait");

	void *mmap_result = mmap(NULL,
				 fb_state->fb_size,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED,
				 fb,
				 0);
	if (mmap_result == MAP_FAILED) {
		ERROR_LOG("fb_initialize: Framebuffer mmap failed. Exiting.");
		return -1;
	}
	fb_state->mapped_fb = mmap_result;

	DEBUG_LOG("fb_initialize: mmapped %d bytes of framebuffer",
		  fb_state->fb_size);

	pthread_t thread;
	int thread_create_result = pthread_create(
		&thread, NULL, fb_async_redraw, fb_state);
#ifdef _GNU_SOURCE
	pthread_setname_np(thread, "screen");
#endif
	if (thread_create_result != 0) {
		ERROR_LOG(
			"fb_initialize: could not initialize async update thread");
		return -1;
	}
	fb_state->redraw_active = true;
	fb_state->redraw_thread = thread;
	
	/* Initialize the mutex properly */
	pthread_mutex_init(&fb_state->fb_mutex, NULL);
	
	/* Initialize update region to "no update pending" */
	fb_state->next_update_x0 = INT_MAX;
	fb_state->next_update_x1 = INT_MIN;
	fb_state->next_update_y0 = INT_MAX;
	fb_state->next_update_y1 = INT_MIN;

	return 0;
}

int fb_finalize(fb_state_t *fb_state)
{
	if (close(fb_state->fb) < 0) {
		DEBUG_LOG("fb_finalize: could not close fb");
		return -1;
	}
	if (munmap(fb_state->mapped_fb, fb_state->fb_size) != 0) {
		DEBUG_LOG("fb_finalize: could not munmap");
		return -1;
	}
	fb_state->redraw_active = false;
	DEBUG_LOG("Waiting for redraw thread to exit");
	pthread_join(fb_state->redraw_thread, NULL);
	DEBUG_LOG("Redraw thread exited");
	pthread_mutex_destroy(&fb_state->fb_mutex);
	return 0;
}

