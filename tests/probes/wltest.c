#include <errno.h>
#include <signal.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define WLTEST_FRAME_GOAL 5
#define WLTEST_TIMEOUT_MS 45000

struct wltest_output {
	struct wlr_output *output;
	struct wl_listener frame;
	struct wl_listener destroy;
};

static struct wl_display *server_display;
static struct wlr_renderer *server_renderer;
static struct wlr_allocator *server_allocator;
static struct wl_listener server_new_output;
static struct wl_listener server_new_backend_destroy;
static int frames_committed;

static void output_handle_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct wltest_output *out = wl_container_of(listener, out, frame);
	struct wlr_output *output = out->output;

	if (!wlr_output_attach_render(output, NULL)) {
		wlr_log(WLR_ERROR, "wltest: attach_render failed");
		wl_display_terminate(server_display);
		return;
	}
	float shade = (frames_committed % WLTEST_FRAME_GOAL) / (float)WLTEST_FRAME_GOAL;
	float color[4] = {0.1f + shade * 0.2f, 0.25f + shade * 0.5f, 0.15f, 1.0f};
	uint32_t rw = (uint32_t)output->width;
	uint32_t rh = (uint32_t)output->height;
	if (!wlr_renderer_begin(server_renderer, rw, rh)) {
		wlr_log(WLR_ERROR, "wltest: renderer_begin failed");
		wlr_output_rollback(output);
		wl_display_terminate(server_display);
		return;
	}
	wlr_renderer_clear(server_renderer, color);
	wlr_renderer_end(server_renderer);

	if (!wlr_output_commit(output)) {
		wlr_log(WLR_ERROR, "wltest: output commit failed");
		wl_display_terminate(server_display);
		return;
	}

	frames_committed++;
	wlr_log(WLR_INFO, "wltest: frame %d committed (%dx%d)", frames_committed,
			output->width, output->height);
	if (frames_committed >= WLTEST_FRAME_GOAL) {
		printf("[wltest] PASS frames=%d\n", frames_committed);
		fflush(stdout);
		wl_display_terminate(server_display);
	}
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct wltest_output *out = wl_container_of(listener, out, destroy);
	wl_list_remove(&out->frame.link);
	wl_list_remove(&out->destroy.link);
	free(out);
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_output *output = data;

	wlr_output_init_render(output, server_allocator, server_renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	if (!wlr_output_commit_state(output, &state)) {
		wlr_log(WLR_ERROR, "wltest: failed to enable output %s", output->name);
		wlr_output_state_finish(&state);
		wl_display_terminate(server_display);
		return;
	}
	wlr_output_state_finish(&state);

	struct wltest_output *out = calloc(1, sizeof(*out));
	if (out == NULL) {
		wl_display_terminate(server_display);
		return;
	}
	out->output = output;
	out->frame.notify = output_handle_frame;
	wl_signal_add(&output->events.frame, &out->frame);
	out->destroy.notify = output_handle_destroy;
	wl_signal_add(&output->events.destroy, &out->destroy);
	wlr_log(WLR_INFO, "wltest: output %s (%dx%d) enabled", output->name,
			output->width, output->height);
}

static void server_handle_backend_destroy(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_ERROR, "wltest: backend destroyed");
	wl_display_terminate(server_display);
}

static int timer_handle_timeout(void *data) {
	(void)data;
	printf("[wltest] FAIL frames=%d (timeout)\n", frames_committed);
	fflush(stdout);
	wl_display_terminate(server_display);
	return 0;
}

static void crash_handler(int sig, siginfo_t *info, void *ctx) {
	ucontext_t *uc = ctx;
	unsigned long rip = (unsigned long)uc->uc_mcontext.gregs[16];
	struct sigaction dfl;
	memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigaction(sig, &dfl, NULL);
	printf("[wltest] crash sig=%d rip=0x%lx addr=%p\n", sig, rip,
	       info ? info->si_addr : NULL);
	fflush(stdout);
}

int main(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	setenv("XDG_RUNTIME_DIR", "/tmp", 0);
	setenv("SEATD_VTBOUND", "0", 1);
	setenv("WLR_RENDERER", "pixman", 1);
	setenv("WLR_BACKENDS", "drm", 1);
	setenv("WLR_DRM_DEVICES", "/dev/dri/card0", 1);
	wlr_log_init(WLR_DEBUG, NULL);

	server_display = wl_display_create();
	if (server_display == NULL) {
		printf("[wltest] FAIL wl_display_create\n");
		return 1;
	}

	struct wlr_backend *backend = wlr_backend_autocreate(server_display, NULL);
	if (backend == NULL) {
		printf("[wltest] FAIL backend create\n");
		return 1;
	}
	server_new_output.notify = server_handle_new_output;
	wl_signal_add(&backend->events.new_output, &server_new_output);
	server_new_backend_destroy.notify = server_handle_backend_destroy;
	wl_signal_add(&backend->events.destroy, &server_new_backend_destroy);

	server_renderer = wlr_renderer_autocreate(backend);
	if (server_renderer == NULL) {
		printf("[wltest] FAIL renderer create\n");
		return 1;
	}
	server_allocator = wlr_allocator_autocreate(backend, server_renderer);
	if (server_allocator == NULL) {
		printf("[wltest] FAIL allocator create\n");
		return 1;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server_display);
	struct wl_event_source *timer =
		wl_event_loop_add_timer(loop, timer_handle_timeout, NULL);
	wl_event_source_timer_update(timer, WLTEST_TIMEOUT_MS);

	if (!wlr_backend_start(backend)) {
		printf("[wltest] FAIL backend start\n");
		return 1;
	}
	wl_display_run(server_display);

	if (frames_committed >= WLTEST_FRAME_GOAL) {
		return 0;
	}
	if (frames_committed == 0) {
		printf("[wltest] FAIL no frames\n");
		fflush(stdout);
	}
	return 1;
}
