/* Exercise the production decoder without opening hardware or starting its thread. */
#include <assert.h>
#include <stdio.h>
#include <libevdev/libevdev.h>

static struct {
	unsigned flags;
	int result;
	struct input_event event;
} reads[16];
static int read_index, read_count;
static int test_next_event(struct libevdev *dev, unsigned flags, struct input_event *ev)
{
	(void)dev;
	assert(read_index < read_count);
	assert(flags == reads[read_index].flags);
	*ev = reads[read_index].event;
	return reads[read_index++].result;
}
#define libevdev_next_event test_next_event
#include "../src/surface/remarkable/input.c"
#undef libevdev_next_event

static input_state_t state;
static unsigned checks;

static void setup(void)
{
	memset(&state, 0, sizeof(state));
	state.model = RM1;
	state.screen_width = 1872;
	state.screen_height = 1404;
	state.orientation = SCREEN_ORIENTATION_LANDSCAPE;
	input_multitouch_state_t *mt = &state.multitouch_state;
	mt->num_slots = 32;
	mt->max_x = 767;
	mt->max_y = 1023;
	mt->primary_slot = -1;
	mt->slots = calloc(mt->num_slots, sizeof(*mt->slots));
	assert(mt->slots);
	for (int i = 0; i < mt->num_slots; i++) mt->slots[i].tracking_id = -1;
	state.pen_state.max_x = 20967;
	state.pen_state.max_y = 15725;
	assert(ring_buf_init(&state.events_buf, 3, sizeof(nsfb_event_t)));
}
static void teardown(void)
{
	free(state.multitouch_state.slots);
	ring_buf_free(&state.events_buf);
}
static void touch(int type, int code, int value)
{
	struct input_event ev = { .type=type, .code=code, .value=value };
	input_process_touch(&state, &ev);
}
static void frame(void) { touch(EV_SYN, SYN_REPORT, 0); }
static void contact(int slot, int id, int x, int y)
{
	touch(EV_ABS, ABS_MT_SLOT, slot);
	touch(EV_ABS, ABS_MT_TRACKING_ID, id);
	touch(EV_ABS, ABS_MT_POSITION_X, x);
	touch(EV_ABS, ABS_MT_POSITION_Y, y);
}
static nsfb_event_t next(enum nsfb_event_type_e type)
{
	nsfb_event_t event;
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	assert(ring_buf_wait(&state.events_buf, &event, &now));
	assert(event.type == type);
	if (type == NSFB_EVENT_KEY_UP || type == NSFB_EVENT_KEY_DOWN)
		assert(event.value.keycode == NSFB_KEY_MOUSE_1);
	checks++;
	return event;
}
static void empty(void)
{
	nsfb_event_t event;
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	assert(!ring_buf_wait(&state.events_buf, &event, &now));
	checks++;
}
static void position(int x, int y)
{
	nsfb_event_t event = next(NSFB_EVENT_MOVE_ABSOLUTE);
	assert(event.value.vector.x == x && event.value.vector.y == y);
}
static void mapping(void)
{
	setup();
	for (int model=RM1; model<=RM2; model++) {
		state.model=model;
		for (int landscape=0; landscape<=1; landscape++) {
			state.orientation=landscape;
			state.screen_width=landscape ? 1872 : 1404;
			state.screen_height=landscape ? 1404 : 1872;
			for (int right=0; right<=1; right++) for (int bottom=0; bottom<=1; bottom++) {
				int x,y;
				input_coordinates(&state, false, right*767, bottom*1023, &x,&y);
				int px=(model==RM1 ? !right : right)*1403;
				int py=(!bottom)*1871;
				assert(x==(landscape ? py : px));
				assert(y==(landscape ? 1403-px : py));
				checks++;
			}
		}
	}
	assert(input_scale(-50, 10, 110, 1404)==0);
	assert(input_scale(200, 10, 110, 1404)==1403);
	assert(input_scale(60, 10, 110, 1404)==701);
	teardown();
}
static void taps(void)
{
	setup();
	/* First tap can be in a nonzero slot and at a coordinate of zero. */
	contact(7, 42, 0, 1023); frame();
	position(0,0); next(NSFB_EVENT_KEY_DOWN); empty();
	/* Minor finger wobble must stay a click instead of starting a drag. */
	touch(EV_ABS, ABS_MT_POSITION_X, 5); frame(); empty();
	touch(EV_ABS, ABS_MT_TRACKING_ID, -1); frame();
	next(NSFB_EVENT_KEY_UP); empty();
	/* Repeat tap: the kernel may omit unchanged coordinates and ABS_MT_SLOT. */
	touch(EV_ABS, ABS_MT_TRACKING_ID, 43); frame();
	position(0,9); next(NSFB_EVENT_KEY_DOWN);
	touch(EV_ABS, ABS_MT_TRACKING_ID, -1); frame(); next(NSFB_EVENT_KEY_UP);
	empty(); teardown();
}
static void multiple_fingers(void)
{
	setup();
	contact(0, 1, 0, 1023); contact(1, 2, 767, 0); frame();
	position(0,0); next(NSFB_EVENT_KEY_DOWN); empty();
	/* Release primary, but finish the frame in a different slot. */
	touch(EV_ABS, ABS_MT_SLOT, 0); touch(EV_ABS, ABS_MT_TRACKING_ID, -1);
	touch(EV_ABS, ABS_MT_SLOT, 1); touch(EV_ABS, ABS_MT_POSITION_Y, 10); frame();
	next(NSFB_EVENT_KEY_UP); empty();
	touch(EV_ABS, ABS_MT_POSITION_X, 700); frame(); empty();
	touch(EV_ABS, ABS_MT_TRACKING_ID, -1); frame(); empty();
	contact(5, 3, 767, 0); frame(); position(1871,1403); next(NSFB_EVENT_KEY_DOWN);
	/* Invalid slots cannot index outside allocated memory. */
	contact(32, 4, 1, 1); frame(); empty();
	contact(-1, 4, 1, 1); frame(); empty();
	touch(EV_ABS, ABS_MT_SLOT, 5); touch(EV_ABS, ABS_MT_TRACKING_ID, -1); frame();
	next(NSFB_EVENT_KEY_UP); empty(); teardown();
}
static void drag_and_replacement(void)
{
	setup(); contact(0, 1, 0, 1023); frame(); position(0,0); next(NSFB_EVENT_KEY_DOWN);
	touch(EV_ABS, ABS_MT_POSITION_X, 100); frame(); position(0,182);
	/* Replacing the same slot must release the original mouse button. */
	contact(0, 2, 767, 0); frame(); next(NSFB_EVENT_KEY_UP); empty();
	touch(EV_ABS, ABS_MT_TRACKING_ID, -1); frame(); empty(); teardown();
}
static void pen(void)
{
	setup();
	struct input_event events[] = {
		{.type=EV_ABS,.code=ABS_X,.value=20967},
		{.type=EV_ABS,.code=ABS_Y,.value=15725},
		{.type=EV_KEY,.code=BTN_TOUCH,.value=1},
		{.type=EV_SYN,.code=SYN_REPORT},
		{.type=EV_KEY,.code=BTN_TOUCH,.value=0},
		{.type=EV_SYN,.code=SYN_REPORT}
	};
	for (size_t i=0;i<sizeof(events)/sizeof(events[0]);i++) input_process_pen(&state,&events[i]);
	position(0,0); next(NSFB_EVENT_KEY_DOWN); next(NSFB_EVENT_KEY_UP); empty();
	/* Pen hover must not move a finger's pressed mouse pointer. */
	contact(0, 1, 767, 0); frame(); position(1871,1403); next(NSFB_EVENT_KEY_DOWN);
	input_process_pen(&state,&events[3]); empty(); teardown();
}
static void sync_recovery(void)
{
	setup(); contact(0, 1, 0, 1023); frame(); position(0,0); next(NSFB_EVENT_KEY_DOWN);
	state.touch_dev=libevdev_new(); assert(state.touch_dev);
	libevdev_set_name(state.touch_dev,"test touchscreen");
	read_index=0; read_count=5;
	reads[0].flags=LIBEVDEV_READ_FLAG_NORMAL; reads[0].result=LIBEVDEV_READ_STATUS_SYNC;
	reads[0].event=(struct input_event){.type=EV_SYN,.code=SYN_DROPPED};
	reads[1].flags=LIBEVDEV_READ_FLAG_SYNC; reads[1].result=LIBEVDEV_READ_STATUS_SYNC;
	reads[1].event=(struct input_event){.type=EV_ABS,.code=ABS_MT_TRACKING_ID,.value=-1};
	reads[2].flags=LIBEVDEV_READ_FLAG_SYNC; reads[2].result=LIBEVDEV_READ_STATUS_SYNC;
	reads[2].event=(struct input_event){.type=EV_SYN,.code=SYN_REPORT};
	reads[3].flags=LIBEVDEV_READ_FLAG_SYNC; reads[3].result=-EAGAIN;
	reads[4].flags=LIBEVDEV_READ_FLAG_NORMAL; reads[4].result=-EAGAIN;
	input_drain(&state,state.touch_dev,&state.touch_sync,input_process_touch);
	assert(read_index==read_count && !state.touch_sync);
	next(NSFB_EVENT_KEY_UP); empty();
	contact(0, 2, 0, 1023); frame(); position(0,0); next(NSFB_EVENT_KEY_DOWN);
	libevdev_free(state.touch_dev); teardown();
}
static void queue(void)
{
	ring_buf_t buf;
	assert(ring_buf_init(&buf, 3, sizeof(int)));
	struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
	int value;
	for (int i=0;i<3;i++) assert(ring_buf_write(&buf,&i));
	for (int i=0;i<2;i++) { assert(ring_buf_wait(&buf,&value,&now)); assert(value==i); }
	for (int i=3;i<1000;i++) assert(ring_buf_write(&buf,&i));
	for (int i=2;i<1000;i++) { assert(ring_buf_wait(&buf,&value,&now)); assert(value==i); checks++; }
	assert(!ring_buf_wait(&buf,&value,&now)); ring_buf_free(&buf);
}
static void *queue_producer(void *arg)
{
	ring_buf_t *buf = arg;
	for (int i = 0; i < 10000; i++) assert(ring_buf_write(buf, &i));
	return NULL;
}
static void queue_threads(void)
{
	ring_buf_t buf;
	pthread_t producer;
	assert(ring_buf_init(&buf, 3, sizeof(int)));
	assert(pthread_create(&producer, NULL, queue_producer, &buf) == 0);
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 30;
	for (int i = 0; i < 10000; i++) {
		int value;
		assert(ring_buf_wait(&buf, &value, &deadline));
		assert(value == i);
	}
	assert(pthread_join(producer, NULL) == 0);
	ring_buf_free(&buf);
	checks++;
}
int main(void)
{
	mapping(); taps(); multiple_fingers(); drag_and_replacement(); pen(); sync_recovery(); queue(); queue_threads();
	printf("reMarkable input: %u checks passed\n",checks);
}
