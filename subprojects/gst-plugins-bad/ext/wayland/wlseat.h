/* GStreamer Wayland video sink
 *
 * Copyright (C) 2022 Vivienne Watermeier.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef __GST_WL_SEAT_H__
#define __GST_WL_SEAT_H__

#include <gst/gst.h>
#include <gst/video/navigation.h>
#include <wayland-client.h>

G_BEGIN_DECLS

typedef struct _GstWlSeat GstWlSeat;
typedef struct _TouchPoint TouchPoint;
typedef enum _PointerEventMask PointerEventMask;
typedef enum _TouchEventType TouchEventType;

enum _PointerEventMask {
	POINTER_EVENT_MOTION         = 1 << 0,
	POINTER_EVENT_BUTTON_PRESS   = 1 << 1,
	POINTER_EVENT_BUTTON_RELEASE = 1 << 2,
	POINTER_EVENT_AXIS_X         = 1 << 3,
	POINTER_EVENT_AXIS_Y         = 1 << 4,
};

enum _TouchEventType {
	TOUCH_EVENT_NONE = 0,
	TOUCH_EVENT_DOWN,
	TOUCH_EVENT_MOTION,
};

struct _TouchPoint {
	guint id;
	TouchEventType type;
	gdouble x, y, pressure;
};

struct _GstWlSeat {
	struct wl_seat *seat;

	struct wl_pointer *pointer;
	guint ptr_event_mask;
	double ptr_x, ptr_y, scroll_x, scroll_y;
	gint mouse_button;

	struct wl_keyboard *kb;
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *kb_state;
	GArray *held_keys;

	struct wl_touch *touch;
	GArray *active_points;

	GstNavigation *navigation;
};

void gst_wl_seat_init (GstWlSeat * self, struct wl_seat *seat);
void gst_wl_seat_destroy (GstWlSeat * self);
void gst_wl_seat_set_interface (GstWlSeat * self, GstNavigation * navigation);

G_END_DECLS

#endif /* __GST_WL_SEAT_H__ */
