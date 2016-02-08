/*
Usage:
$THIS_PROGRAM $SOUND_FILE
It connects to a X11 server and plays SOUND_FILE by GStreamer when the XkbBellNotify event occurs. This program has the same purpose as "module-x11-bell" of PulseAudio ( https://wiki.freedesktop.org/www/Software/PulseAudio/Documentation/User/Modules/#index30h3 ) and allows more intricate configurations. For example, when 2 X11 servers play on 1 sound card, or when 1 X11 server plays on 2 sound cards.
Modules for "pkg-config": "xcb-xkb >= 1", "gstreamer-1.0 >= 1".
It seems that the standard X11 bell is not enabled after this program terminates because of an error in the Xorg server. See https://bugs.freedesktop.org/show_bug.cgi?id=93568 .
The official name of this program is "x11-bell-gst".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xkb.h>
#include <gst/gst.h>
typedef struct {
	GMainLoop *loop;
	GstElement *player;
} play_data_t;
static gboolean on_eos(GstBus *bus, GstMessage *message, gpointer loop) {
	g_main_loop_quit((GMainLoop *)loop);
	return TRUE;
}
void play(play_data_t *play_data) {
	gst_element_set_state(play_data->player, GST_STATE_PLAYING);
	g_main_loop_run(play_data->loop);
	gst_element_set_state(play_data->player, GST_STATE_NULL);
}

typedef uint8_t get_event_type_data_t;
typedef enum {EVENT_TYPE_OTHER, EVENT_TYPE_ERROR, EVENT_TYPE_BELL_NOTIFY} event_type_t;
event_type_t get_event_type(const get_event_type_data_t *user_data, xcb_generic_event_t *event) {
	int r;
	if (0 == event->response_type) {
		r = EVENT_TYPE_ERROR;
	} else if ((event->response_type & 0x7f) != *user_data) {
		r = EVENT_TYPE_OTHER;
	} else if (event->pad0 != XCB_XKB_BELL_NOTIFY) {
		r = EVENT_TYPE_OTHER;
	} else {
		r = EVENT_TYPE_BELL_NOTIFY;
	}
	return r;
}

typedef enum
	{ LOOP_DONE = 0
	, LOOP_WAIT_NULL
	, LOOP_X11_ERROR
	, LOOP_POLL_CONN_ERROR
	} loop_result_t;
loop_result_t loop_my(xcb_connection_t *conn, get_event_type_data_t *get_event_type_data
	, play_data_t *play_data) {
	xcb_generic_event_t *event;
	loop_result_t error;
	gboolean exit;
	while (TRUE) {
		while (TRUE) {
			event = xcb_wait_for_event(conn);
			if (! event) {
				exit = TRUE;
				error = LOOP_WAIT_NULL;
			} else {
				switch (get_event_type(get_event_type_data, event)) {
					case EVENT_TYPE_ERROR:
						exit = TRUE;
						error = LOOP_X11_ERROR;
						break;
					case EVENT_TYPE_OTHER:
						exit = FALSE;
						break;
					case EVENT_TYPE_BELL_NOTIFY:
						exit = TRUE;
						error = LOOP_DONE;
						break;
				}
				free(event);
			}
			if (exit) break;
		}
		if (error) break;
		play(play_data);
		while (TRUE) {
			event = xcb_poll_for_event(conn);
			if (! event) {
				if (xcb_connection_has_error(conn)) {
					exit = TRUE;
					error = LOOP_POLL_CONN_ERROR;
				} else {
					exit = TRUE;
					error = LOOP_DONE;
				}
			} else {
				switch (get_event_type(get_event_type_data, event)) {
					case EVENT_TYPE_ERROR:
						exit = TRUE;
						error = LOOP_X11_ERROR;
						break;
					case EVENT_TYPE_OTHER:
					case EVENT_TYPE_BELL_NOTIFY:
						exit = FALSE;
						break;
				}
				free(event);
			}
			if (exit) break;
		}
		if (error) break;
	}
	return error;
}

typedef enum
	{ MAIN_SUCCESS = 0
	, MAIN_ERROR_X11 = 220
	, MAIN_ERROR_GST = 221
	, MAIN_ERROR_ARG = 222
	} main_result_t;
main_result_t main_xcb(play_data_t *play_data) {
	main_result_t error_ret;
	xcb_connection_t *conn;
	xcb_generic_error_t *error;
	xcb_query_extension_cookie_t query_xkb_cookie;
	xcb_query_extension_reply_t *query_xkb_reply;
	uint8_t query_xkb_present;
	xcb_xkb_use_extension_cookie_t use_xkb_cookie;
	xcb_xkb_use_extension_reply_t *use_xkb_reply;
	uint8_t use_xkb_supported;
	uint8_t xkb_event_response_type;
	xcb_xkb_per_client_flags_cookie_t pcf_cookie;
	xcb_xkb_per_client_flags_reply_t *pcf_reply;
	xcb_void_cookie_t select_events_cookie;
	uint8_t per_key_repeat[0];
	loop_result_t loop_r;
	conn = xcb_connect(NULL, NULL);
	use_xkb_cookie = xcb_xkb_use_extension_unchecked(conn, 1, 0);
	select_events_cookie = xcb_xkb_select_events_checked(conn, XCB_XKB_ID_USE_CORE_KBD
		, XCB_XKB_EVENT_TYPE_BELL_NOTIFY, 0, XCB_XKB_EVENT_TYPE_BELL_NOTIFY
		, 0, 0, NULL);
	pcf_cookie = xcb_xkb_per_client_flags(conn, XCB_XKB_ID_USE_CORE_KBD
		, XCB_XKB_PER_CLIENT_FLAG_AUTO_RESET_CONTROLS, XCB_XKB_PER_CLIENT_FLAG_AUTO_RESET_CONTROLS
		, XCB_XKB_BOOL_CTRL_AUDIBLE_BELL_MASK, XCB_XKB_BOOL_CTRL_AUDIBLE_BELL_MASK, XCB_XKB_BOOL_CTRL_AUDIBLE_BELL_MASK);
	query_xkb_cookie = xcb_query_extension_unchecked(conn, strlen(xcb_xkb_id.name), xcb_xkb_id.name);
	use_xkb_reply = xcb_xkb_use_extension_reply(conn, use_xkb_cookie, NULL);
	if (! use_xkb_reply) {
		error_ret = MAIN_ERROR_X11;
	} else {
		use_xkb_supported = use_xkb_reply->supported;
		free(use_xkb_reply);
		if (! use_xkb_supported) {
			error_ret = MAIN_ERROR_X11;
		} else {
			if (error = xcb_request_check(conn, select_events_cookie)) {
				free(error);
				error_ret = MAIN_ERROR_X11;
			} else {
				pcf_reply = xcb_xkb_per_client_flags_reply(conn, pcf_cookie, &error);
				if (! pcf_reply) {
					if (error) { free(error); }
					error_ret = MAIN_ERROR_X11;
				} else {
					/*
					printf("autoCtrls: %x, autoCtrlsValues: %x\n", pcf_reply->autoCtrls, pcf_reply->autoCtrlsValues);
					fflush(stdout);
					*/
					free(pcf_reply);
					xcb_xkb_set_controls(conn, XCB_XKB_ID_USE_CORE_KBD
						, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
						, XCB_XKB_BOOL_CTRL_AUDIBLE_BELL_MASK, 0
						, XCB_XKB_CONTROL_CONTROLS_ENABLED
						, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, per_key_repeat);
					query_xkb_reply = xcb_query_extension_reply(conn, query_xkb_cookie, NULL);
					if (! query_xkb_reply) {
						error_ret = MAIN_ERROR_X11;
					} else {
						xkb_event_response_type = query_xkb_reply->first_event;
						free(query_xkb_reply);
						xcb_flush(conn);
						loop_r = loop_my(conn, &xkb_event_response_type, play_data);
						switch (loop_r) {
							case LOOP_WAIT_NULL:
								error_ret = MAIN_SUCCESS;
								break;
							case LOOP_X11_ERROR:
								error_ret = MAIN_ERROR_X11;
								break;
							case LOOP_POLL_CONN_ERROR:
								if (XCB_CONN_ERROR == xcb_connection_has_error(conn)) {
									error_ret = MAIN_SUCCESS;
								} else {
									error_ret = MAIN_ERROR_X11;
								}
								break;
						}
					}
				}
			}
		}
	}
	xcb_disconnect(conn);
	switch (error_ret) {
		case MAIN_SUCCESS: break;
		case MAIN_ERROR_X11:
			fprintf(stderr, "A X11 error.\n");
			break;
	}
	return error_ret;
}

int main(int argc, char *argv[]) {
	main_result_t error_ret;
	guint gst_ver0, gst_ver1, gst_ver2, gst_ver3;
	GMainLoop *loop;
	GstElement *player;
	GstBus *bus;
	play_data_t play_data;
	gst_init(&argc, &argv);
	gst_version(&gst_ver0, &gst_ver1, &gst_ver2, &gst_ver3);
	if (gst_ver0 < 1) {
		error_ret = MAIN_ERROR_GST;
		fprintf(stderr, "The version of GStreamer is old.\n");
	} else {
		if (argc != 2) {
			error_ret = MAIN_ERROR_ARG;
			fprintf(stderr, "Invalid arguments.\n");
		} else {
			loop = g_main_loop_new(NULL, FALSE);
			player = gst_element_factory_make("playbin", NULL);
			g_object_set(G_OBJECT(player), "uri", argv[1], NULL);
			bus = gst_pipeline_get_bus(GST_PIPELINE(player));
			gst_bus_add_signal_watch(bus);
			g_signal_connect(bus, "message::eos", G_CALLBACK(on_eos), loop);
			gst_object_unref(bus);
			play_data.loop = loop;
			play_data.player = player;
			error_ret = main_xcb(&play_data);
			gst_object_unref(GST_OBJECT(player));
			g_main_loop_unref(loop);
		}
	}
	return error_ret;
}

