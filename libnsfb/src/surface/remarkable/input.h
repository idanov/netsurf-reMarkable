#ifndef RM_INPUT_H
#define RM_INPUT_H

#include <libevdev/libevdev.h>
#include <stdbool.h>
#include <pthread.h>

#include "libnsfb.h"
#include "ringbuf.h"
#include "screen.h"

#define EVENTS_DIR "/dev/input"
#define RM1_MACHINE_NAME_1 "reMarkable 1.0"
#define RM1_MACHINE_NAME_2 "reMarkable Prototype 1"
#define RM2_MACHINE_NAME "reMarkable 2.0"
#define MAX_EVENT_POLL_TIMEOUT_MS 250

/* Raw axes are retained: evdev only sends values which have changed. */
typedef struct input_single_state_s {
	int tracking_id;
	int x, y;
} input_single_state_t;

typedef struct input_pen_state_s {
	int min_x, max_x, min_y, max_y;
	int x, y;
	bool touched;
	bool mouse_down;
} input_pen_state_t;

typedef struct input_multitouch_state_s {
	int min_x, max_x, min_y, max_y;
	int current_slot;
	int num_slots;
	input_single_state_t *slots;
	int primary_slot;
	int primary_tracking_id;
	bool gesture_active;
	bool dragging;
	int press_x, press_y;
} input_multitouch_state_t;

typedef struct input_state_s {
	enum { RM1, RM2 } model;
	struct libevdev *pen_dev, *gpio_dev, *touch_dev;
	bool pen_sync, gpio_sync, touch_sync;
	ring_buf_t events_buf;
	input_multitouch_state_t multitouch_state;
	input_pen_state_t pen_state;
	bool poll_active;
	pthread_mutex_t poll_mutex;
	pthread_t poll_thread;
	int screen_width, screen_height;
	screen_orientation_t orientation;
	bool trace;
	bool pointer_valid;
	int pointer_x, pointer_y;
} input_state_t;

int input_initialize(input_state_t *input_state, nsfb_t *nsfb);
int input_finalize(input_state_t *input_state);
bool input_get_next_event(input_state_t *input_state, nsfb_event_t *event,
			  int timeout);
#endif
