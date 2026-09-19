#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>

#include "log.h"
#include "input.h"
#include "libnsfb_event.h"
#include "nsfb.h"

/* Finger jitter should not turn a tap into NetSurf's five-pixel mouse drag. */
#define TOUCH_DRAG_THRESHOLD 20

static int input_scale(int value, int min, int max, int extent)
{
	if (value < min) value = min;
	if (value > max) value = max;
	return (int)(((int64_t)value - min) * (extent - 1) / (max - min));
}

static void input_coordinates(input_state_t *state, bool pen, int raw_x,
		int raw_y, int *x, int *y)
{
	int width = state->orientation == SCREEN_ORIENTATION_LANDSCAPE ?
		state->screen_height : state->screen_width;
	int height = state->orientation == SCREEN_ORIENTATION_LANDSCAPE ?
		state->screen_width : state->screen_height;
	int px, py;
	if (pen) {
		input_pen_state_t *p = &state->pen_state;
		px = input_scale(raw_y, p->min_y, p->max_y, width);
		py = height - 1 - input_scale(raw_x, p->min_x, p->max_x, height);
	} else {
		input_multitouch_state_t *mt = &state->multitouch_state;
		px = input_scale(raw_x, mt->min_x, mt->max_x, width);
		if (state->model == RM1) px = width - 1 - px;
		py = height - 1 - input_scale(raw_y, mt->min_y, mt->max_y, height);
	}
	/* Inverse of the RGB565 plotter: physical = (width - 1 - y, x). */
	*x = state->orientation == SCREEN_ORIENTATION_LANDSCAPE ? py : px;
	*y = state->orientation == SCREEN_ORIENTATION_LANDSCAPE ? width - 1 - px : py;
}

static void input_position(input_state_t *state, int x, int y, bool force)
{
	if (!force && state->pointer_valid &&
	    x == state->pointer_x && y == state->pointer_y) return;
	nsfb_event_t event = { .type = NSFB_EVENT_MOVE_ABSOLUTE };
	event.value.vector.x = x;
	event.value.vector.y = y;
	if (ring_buf_write(&state->events_buf, &event)) {
		state->pointer_valid = true;
		state->pointer_x = x;
		state->pointer_y = y;
	}
}

static void input_button(input_state_t *state, bool down, const char *source)
{
	nsfb_event_t event = { .type = down ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP };
	event.value.keycode = NSFB_KEY_MOUSE_1;
	ring_buf_write(&state->events_buf, &event);
	if (state->trace) {
		fprintf(stderr, "INPUT: %s %s at %d,%d\n", source,
			down ? "down" : "up", state->pointer_x, state->pointer_y);
	}
}

static void input_touch_frame(input_state_t *state)
{
	input_multitouch_state_t *mt = &state->multitouch_state;
	int first = -1;
	for (int i = 0; i < mt->num_slots; i++) {
		if (mt->slots[i].tracking_id >= 0) {
			first = i;
			break;
		}
	}

	if (mt->primary_slot >= 0) {
		input_single_state_t *slot = &mt->slots[mt->primary_slot];
		bool released = slot->tracking_id != mt->primary_tracking_id;
		/* On release, keep the last complete position of this contact.
		 * The slot may already contain a replacement finger's coordinates. */
		if (released) {
			input_button(state, false, "touch");
			mt->primary_slot = -1;
		} else {
			int x, y;
			input_coordinates(state, false, slot->x, slot->y, &x, &y);
			if (abs(x - mt->press_x) > TOUCH_DRAG_THRESHOLD ||
			    abs(y - mt->press_y) > TOUCH_DRAG_THRESHOLD) {
				mt->dragging = true;
			}
			if (mt->dragging) input_position(state, x, y, false);
		}
	}

	/* One mouse button belongs to one finger until all fingers are lifted.
	 * Never promote a second finger into an accidental new click. */
	if (!mt->gesture_active && first >= 0 && !state->pen_state.mouse_down) {
		input_single_state_t *slot = &mt->slots[first];
		mt->primary_slot = first;
		mt->primary_tracking_id = slot->tracking_id;
		mt->dragging = false;
		input_coordinates(state, false, slot->x, slot->y,
				&mt->press_x, &mt->press_y);
		input_position(state, mt->press_x, mt->press_y, true);
		input_button(state, true, "touch");
	}
	mt->gesture_active = first >= 0;
}

static void input_process_touch(input_state_t *state, const struct input_event *ev)
{
	input_multitouch_state_t *mt = &state->multitouch_state;
	if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
		input_touch_frame(state);
		return;
	}
	if (ev->type != EV_ABS) return; /* BTN_TOUCH/MSC are redundant for MT-B. */
	if (ev->code == ABS_MT_SLOT) {
		mt->current_slot = ev->value >= 0 && ev->value < mt->num_slots ?
			ev->value : -1;
		return;
	}
	if (mt->current_slot < 0 || mt->current_slot >= mt->num_slots) return;
	input_single_state_t *slot = &mt->slots[mt->current_slot];
	switch (ev->code) {
	case ABS_MT_TRACKING_ID: slot->tracking_id = ev->value; break;
	case ABS_MT_POSITION_X: slot->x = ev->value; break;
	case ABS_MT_POSITION_Y: slot->y = ev->value; break;
	default: break; /* Pressure, contact size and orientation are optional. */
	}
}

static void input_process_pen(input_state_t *state, const struct input_event *ev)
{
	input_pen_state_t *pen = &state->pen_state;
	if (ev->type == EV_ABS) {
		if (ev->code == ABS_X) pen->x = ev->value;
		if (ev->code == ABS_Y) pen->y = ev->value;
	} else if (ev->type == EV_KEY && ev->code == BTN_TOUCH) {
		pen->touched = ev->value != 0;
	} else if (ev->type == EV_SYN && ev->code == SYN_REPORT &&
		   state->multitouch_state.primary_slot < 0) {
		int x, y;
		input_coordinates(state, true, pen->x, pen->y, &x, &y);
		/* Preserve the contact position when the pen leaves the surface. */
		if (!pen->touched && pen->mouse_down) {
			input_button(state, false, "pen");
			pen->mouse_down = false;
		} else {
			input_position(state, x, y, pen->touched && !pen->mouse_down);
			if (pen->touched && !pen->mouse_down) {
				input_button(state, true, "pen");
				pen->mouse_down = true;
			}
		}
	}
}

static void input_process_gpio(input_state_t *state, const struct input_event *ev)
{
	if (ev->type != EV_KEY) return;
	nsfb_event_t event = { .type = ev->value ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP };
	switch (ev->code) {
	case KEY_LEFT: event.value.keycode = NSFB_KEY_PAGEUP; break;
	case KEY_HOME: event.value.keycode = NSFB_KEY_HOME; break;
	case KEY_RIGHT: event.value.keycode = NSFB_KEY_PAGEDOWN; break;
	default: return;
	}
	ring_buf_write(&state->events_buf, &event);
}

static void input_drain(input_state_t *state, struct libevdev *dev, bool *sync,
		void (*process)(input_state_t *, const struct input_event *))
{
	struct input_event ev;
	int rc;
	while (true) {
		rc = libevdev_next_event(dev, *sync ? LIBEVDEV_READ_FLAG_SYNC :
				LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == -EAGAIN) {
			if (*sync) { *sync = false; continue; }
			return;
		}
		if (rc == -EINTR) continue;
		if (rc < 0) {
			ERROR_LOG("input: reading %s: %s", libevdev_get_name(dev), strerror(-rc));
			return;
		}
		if (rc == LIBEVDEV_READ_STATUS_SYNC) *sync = true;
		if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
			DEBUG_LOG("input: resynchronizing %s", libevdev_get_name(dev));
			continue;
		}
		process(state, &ev);
	}
}

static void *input_async_handler(void *context)
{
	input_state_t *state = context;
	const struct timespec delay = { .tv_sec = 0, .tv_nsec = 10000000 };
	while (true) {
		pthread_mutex_lock(&state->poll_mutex);
		bool active = state->poll_active;
		pthread_mutex_unlock(&state->poll_mutex);
		if (!active) break;
		input_drain(state, state->touch_dev, &state->touch_sync, input_process_touch);
		input_drain(state, state->gpio_dev, &state->gpio_sync, input_process_gpio);
		input_drain(state, state->pen_dev, &state->pen_sync, input_process_pen);
		nanosleep(&delay, NULL);
	}
	return NULL;
}

static void input_close_device(struct libevdev **dev)
{
	if (*dev == NULL) return;
	close(libevdev_get_fd(*dev));
	libevdev_free(*dev);
	*dev = NULL;
}

static int input_identify_input_devices(input_state_t *state)
{
	char machine[50];
	int fd = open("/sys/devices/soc0/machine", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	ssize_t length = read(fd, machine, sizeof(machine) - 1);
	close(fd);
	if (length <= 0) return -1;
	machine[length] = '\0';
	if (strncmp(machine, RM1_MACHINE_NAME_1, strlen(RM1_MACHINE_NAME_1)) == 0 ||
	    strncmp(machine, RM1_MACHINE_NAME_2, strlen(RM1_MACHINE_NAME_2)) == 0) {
		state->model = RM1;
	} else if (strncmp(machine, RM2_MACHINE_NAME, strlen(RM2_MACHINE_NAME)) == 0) {
		state->model = RM2;
	} else {
		ERROR_LOG("input: unknown machine %s", machine);
		return -1;
	}
	DIR *dir = opendir(EVENTS_DIR);
	if (dir == NULL) return -1;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "event", 5) != 0) continue;
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", EVENTS_DIR, entry->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) {
			ERROR_LOG("input: opening %s: %s", path, strerror(errno));
			continue;
		}
		struct libevdev *dev = NULL;
		if (libevdev_new_from_fd(fd, &dev) < 0) { close(fd); continue; }
		struct libevdev **target = NULL;
		if (libevdev_has_event_code(dev, EV_ABS, ABS_MT_SLOT) &&
		    libevdev_has_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID) &&
		    libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_X) &&
		    libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_Y)) {
			target = &state->touch_dev;
		} else if (libevdev_has_event_code(dev, EV_KEY, BTN_STYLUS)) {
			target = &state->pen_dev;
		} else if (libevdev_has_event_code(dev, EV_KEY, KEY_POWER)) {
			target = &state->gpio_dev;
		}
		if (target != NULL && *target == NULL) {
			*target = dev;
			DEBUG_LOG("input: using %s (%s)", path, libevdev_get_name(dev));
		} else {
			input_close_device(&dev);
		}
	}
	closedir(dir);
	return state->touch_dev && state->pen_dev && state->gpio_dev ? 0 : -1;
}

static bool input_axis_range(struct libevdev *dev, int code, int *min, int *max)
{
	const struct input_absinfo *info = libevdev_get_abs_info(dev, code);
	if (info == NULL || info->maximum <= info->minimum) return false;
	*min = info->minimum;
	*max = info->maximum;
	return true;
}

int input_initialize(input_state_t *state, nsfb_t *nsfb)
{
	memset(state, 0, sizeof(*state));
	if (input_identify_input_devices(state) != 0) goto fail;
	input_multitouch_state_t *mt = &state->multitouch_state;
	input_pen_state_t *pen = &state->pen_state;
	mt->num_slots = libevdev_get_num_slots(state->touch_dev);
	if (mt->num_slots <= 0 ||
	    !input_axis_range(state->touch_dev, ABS_MT_POSITION_X, &mt->min_x, &mt->max_x) ||
	    !input_axis_range(state->touch_dev, ABS_MT_POSITION_Y, &mt->min_y, &mt->max_y) ||
	    !input_axis_range(state->pen_dev, ABS_X, &pen->min_x, &pen->max_x) ||
	    !input_axis_range(state->pen_dev, ABS_Y, &pen->min_y, &pen->max_y)) goto fail;
	mt->slots = calloc(mt->num_slots, sizeof(*mt->slots));
	if (mt->slots == NULL) goto fail;
	mt->primary_slot = -1;
	mt->current_slot = libevdev_get_current_slot(state->touch_dev);
	for (int i = 0; i < mt->num_slots; i++) {
		mt->slots[i].tracking_id = libevdev_get_slot_value(state->touch_dev, i, ABS_MT_TRACKING_ID);
		mt->slots[i].x = libevdev_get_slot_value(state->touch_dev, i, ABS_MT_POSITION_X);
		mt->slots[i].y = libevdev_get_slot_value(state->touch_dev, i, ABS_MT_POSITION_Y);
		if (mt->slots[i].tracking_id >= 0) mt->gesture_active = true;
	}
	pen->x = libevdev_get_event_value(state->pen_dev, EV_ABS, ABS_X);
	pen->y = libevdev_get_event_value(state->pen_dev, EV_ABS, ABS_Y);
	pen->touched = libevdev_get_event_value(state->pen_dev, EV_KEY, BTN_TOUCH) != 0;
	state->screen_width = nsfb->width;
	state->screen_height = nsfb->height;
	state->orientation = nsfb->orientation ? SCREEN_ORIENTATION_LANDSCAPE : SCREEN_ORIENTATION_PORTRAIT;
	state->trace = getenv("NETSURF_RM_INPUT_TRACE") != NULL;
	DEBUG_LOG("input: RM%d %s, %d slots, touch x=%d..%d y=%d..%d",
		state->model == RM1 ? 1 : 2,
		state->orientation == SCREEN_ORIENTATION_LANDSCAPE ? "landscape" : "portrait",
		mt->num_slots, mt->min_x, mt->max_x, mt->min_y, mt->max_y);
	if (!ring_buf_init(&state->events_buf, 50, sizeof(nsfb_event_t))) goto fail;
	if (pthread_mutex_init(&state->poll_mutex, NULL) != 0) {
		ring_buf_free(&state->events_buf);
		goto fail;
	}
	state->poll_active = true;
	int rc = pthread_create(&state->poll_thread, NULL, input_async_handler, state);
	if (rc != 0) {
		pthread_mutex_destroy(&state->poll_mutex);
		ring_buf_free(&state->events_buf);
		goto fail;
	}
#ifdef _GNU_SOURCE
	pthread_setname_np(state->poll_thread, "input");
#endif
	return 0;
fail:
	ERROR_LOG("input: could not initialize input devices");
	free(state->multitouch_state.slots);
	input_close_device(&state->touch_dev);
	input_close_device(&state->pen_dev);
	input_close_device(&state->gpio_dev);
	return -1;
}

bool input_get_next_event(input_state_t *state, nsfb_event_t *event, int timeout)
{
	/* NetSurf uses -1 for an indefinite wait. Keep servicing its scheduler. */
	if (timeout < 0 || timeout > MAX_EVENT_POLL_TIMEOUT_MS)
		timeout = MAX_EVENT_POLL_TIMEOUT_MS;
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_nsec += timeout * 1000000;
	deadline.tv_sec += deadline.tv_nsec / 1000000000;
	deadline.tv_nsec %= 1000000000;
	return ring_buf_wait(&state->events_buf, event, &deadline);
}

int input_finalize(input_state_t *state)
{
	pthread_mutex_lock(&state->poll_mutex);
	state->poll_active = false;
	pthread_mutex_unlock(&state->poll_mutex);
	pthread_join(state->poll_thread, NULL);
	pthread_mutex_destroy(&state->poll_mutex);
	input_close_device(&state->gpio_dev);
	input_close_device(&state->pen_dev);
	input_close_device(&state->touch_dev);
	free(state->multitouch_state.slots);
	ring_buf_free(&state->events_buf);
	return 0;
}
