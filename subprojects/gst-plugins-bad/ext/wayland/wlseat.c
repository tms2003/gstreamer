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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "wlseat.h"
#include "wldisplay.h"

#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

static void seat_name (void *data, struct wl_seat *seat, const char *name);
static void seat_capabilities (void *data, struct wl_seat *seat,
    uint32_t capabilities);

static void pointer_enter (void *data, struct wl_pointer *pointer,
    uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void pointer_leave (void *data, struct wl_pointer *pointer,
    uint32_t serial, struct wl_surface *surface);
static void pointer_motion (void *data, struct wl_pointer *pointer,
    uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void pointer_button (void *data, struct wl_pointer *pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void pointer_axis (void *data, struct wl_pointer *pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value);
static void pointer_axis_source (void *data, struct wl_pointer *pointer,
    uint32_t axis_source);
static void pointer_axis_stop (void *data, struct wl_pointer *pointer,
    uint32_t time, uint32_t axis);
static void pointer_axis_discrete (void *data, struct wl_pointer *pointer,
    uint32_t axis, int32_t discrete);
static void pointer_frame (void *data, struct wl_pointer *pointer);

#define KEY_NAME_SIZE 64

static void kb_keymap (void *data, struct wl_keyboard *kb, uint32_t format,
    int32_t fd, uint32_t size);
static void kb_enter (void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface, struct wl_array *keys);
static void kb_leave (void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface);
static void kb_key (void *data, struct wl_keyboard *kb, uint32_t serial,
    uint32_t time, xkb_keycode_t key_code, uint32_t state);
static void kb_modifiers (void *data, struct wl_keyboard *kb, uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
    uint32_t group);
static void kb_repeat_info (void *data, struct wl_keyboard *kb, int32_t rate,
    int32_t delay);

static void touch_down (void *data, struct wl_touch *touch, uint32_t serial,
    uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x,
    wl_fixed_t y);
static void touch_up (void *data, struct wl_touch *touch, uint32_t serial,
    uint32_t time, int32_t id);
static void touch_motion (void *data, struct wl_touch *touch, uint32_t time,
    int32_t id, wl_fixed_t x, wl_fixed_t y);
static void touch_shape (void *data, struct wl_touch *touch, int32_t id,
    wl_fixed_t major, wl_fixed_t minor);
static void touch_orientation (void *data, struct wl_touch *touch, int32_t id,
    wl_fixed_t orientation);
static void touch_cancel (void *data, struct wl_touch *touch);
static void touch_frame (void *data, struct wl_touch *touch);

static const struct wl_seat_listener seat_listener = {
  seat_capabilities,
  seat_name,
};

static const struct wl_pointer_listener pointer_listener = {
  pointer_enter,
  pointer_leave,
  pointer_motion,
  pointer_button,
  pointer_axis,
  pointer_frame,
  pointer_axis_source,
  pointer_axis_stop,
  pointer_axis_discrete,
};

static const struct wl_keyboard_listener kb_listener = {
  kb_keymap,
  kb_enter,
  kb_leave,
  kb_key,
  kb_modifiers,
  kb_repeat_info,
};

static const struct wl_touch_listener touch_listener = {
  touch_down,
  touch_up,
  touch_motion,
  touch_frame,
  touch_cancel,
  touch_shape,
  touch_orientation,
};

void
gst_wl_seat_init (GstWlSeat * self, struct wl_seat *seat)
{
  self->seat = seat;
  wl_seat_add_listener (seat, &seat_listener, self);

  self->xkb = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  self->keymap = NULL;
  self->kb_state = NULL;
  self->held_keys = g_array_new (FALSE, FALSE, sizeof (xkb_keycode_t));
  self->active_points = g_array_new (FALSE, FALSE, sizeof (TouchPoint));

  self->pointer = NULL;
  self->kb = NULL;

  self->navigation = NULL;
}

void
gst_wl_seat_destroy (GstWlSeat * self)
{
  xkb_state_unref (self->kb_state);
  xkb_keymap_unref (self->keymap);
  xkb_context_unref (self->xkb);
  g_array_free (self->held_keys, TRUE);
  g_array_free (self->active_points, TRUE);

  wl_pointer_destroy (self->pointer);
  wl_keyboard_destroy (self->kb);

  wl_seat_destroy (self->seat);
}

void
gst_wl_seat_set_interface (GstWlSeat * self, GstNavigation * navigation)
{
  self->navigation = navigation;
}

static void
seat_name (void *data, struct wl_seat *wl_seat, const char *name)
{
  /* do nothing */
}

static void
seat_capabilities (void *data, struct wl_seat *wl_seat, uint32_t capabilities)
{
  GstWlSeat *self = data;
  gboolean has_cap;

  has_cap = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (has_cap && self->pointer == NULL) {
    self->ptr_event_mask = 0;
    self->ptr_x = 0;
    self->ptr_y = 0;
    self->scroll_x = 0;
    self->scroll_y = 0;
    self->mouse_button = 0;
    self->pointer = wl_seat_get_pointer (self->seat);
    wl_pointer_add_listener (self->pointer, &pointer_listener, self);
  } else if (!has_cap && self->pointer != NULL) {
    wl_pointer_release (self->pointer);
    self->pointer = NULL;
  }

  has_cap = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
  if (has_cap && self->kb == NULL) {
    self->keymap = NULL;
    self->kb = wl_seat_get_keyboard (self->seat);
    wl_keyboard_add_listener (self->kb, &kb_listener, self);
  } else if (!has_cap && self->pointer != NULL) {
    xkb_keymap_unref (self->keymap);
    xkb_state_unref (self->kb_state);
    g_array_set_size (self->held_keys, 0);
    wl_keyboard_release (self->kb);
    self->kb = NULL;
  }

  has_cap = capabilities & WL_SEAT_CAPABILITY_TOUCH;
  if (has_cap && self->touch == NULL) {
    self->touch = wl_seat_get_touch (self->seat);
    wl_touch_add_listener (self->touch, &touch_listener, self);
  } else if (!has_cap && self->pointer != NULL) {
    g_array_set_size (self->active_points, 0);
    wl_touch_release (self->touch);
    self->touch = NULL;
  }
}

static void
pointer_enter (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
  GstWlSeat *self = data;
  gdouble xpos = wl_fixed_to_double (x);
  gdouble ypos = wl_fixed_to_double (y);

  GST_DEBUG ("received pointer-enter event at %f,%f", xpos, ypos);

  self->ptr_event_mask |= POINTER_EVENT_MOTION;
  self->ptr_x = xpos;
  self->ptr_y = ypos;
}

static void
pointer_leave (void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface)
{
  GST_DEBUG ("received pointer-leave event");
}

static void
pointer_motion (void *data, struct wl_pointer *pointer, uint32_t time,
    wl_fixed_t x, wl_fixed_t y)
{
  GstWlSeat *self = data;
  gdouble xpos = wl_fixed_to_double (x);
  gdouble ypos = wl_fixed_to_double (y);

  GST_DEBUG ("received pointer-motion event at %f,%f", xpos, ypos);

  self->ptr_event_mask |= POINTER_EVENT_MOTION;
  self->ptr_x = xpos;
  self->ptr_y = ypos;
}

static void
pointer_button (void *data, struct wl_pointer *pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
  GstWlSeat *self = data;

  GST_DEBUG ("received pointer-button event for %u with state %u", button,
      state);

  if (state == 0)
    self->ptr_event_mask |= POINTER_EVENT_BUTTON_RELEASE;
  else
    self->ptr_event_mask |= POINTER_EVENT_BUTTON_PRESS;

  self->mouse_button = (gint) button;
}

static void
pointer_axis (void *data, struct wl_pointer *pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value)
{
  GstWlSeat *self = data;
  gdouble dist = wl_fixed_to_double (value);

  GST_DEBUG ("received pointer-axis event for %u with value %f", axis, dist);

  if (axis == 0) {
    self->ptr_event_mask |= POINTER_EVENT_AXIS_Y;
    self->scroll_y = dist;
  } else {
    self->ptr_event_mask |= POINTER_EVENT_AXIS_X;
    self->scroll_x = dist;
  }
}

static void
pointer_axis_source (void *data, struct wl_pointer *pointer,
    uint32_t axis_source)
{
  GST_DEBUG ("received pointer-axis-source event with source %u", axis_source);
}

static void
pointer_axis_stop (void *data, struct wl_pointer *pointer, uint32_t time,
    uint32_t axis)
{
  GST_DEBUG ("received pointer-axis-stop event for axis %u", axis);
}

static void
pointer_axis_discrete (void *data, struct wl_pointer *pointer, uint32_t axis,
    int32_t discrete)
{
  GST_DEBUG ("received pointer-axis-discrete event for axis %u with value %d",
      axis, discrete);
}

static void
pointer_frame (void *data, struct wl_pointer *pointer)
{
  GstWlSeat *self = data;

  GST_DEBUG ("received pointer-frame event");

  if (self->navigation) {
    if (self->ptr_event_mask & POINTER_EVENT_MOTION) {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_mouse_move (self->ptr_x, self->ptr_y));
      GST_DEBUG ("sent mouse-move event at %f,%f", self->ptr_x, self->ptr_y);
    }

    if (self->ptr_event_mask & POINTER_EVENT_BUTTON_PRESS) {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_mouse_button_press (self->mouse_button,
              self->ptr_x, self->ptr_y));
      GST_DEBUG ("sent mouse-button-press event at %f,%f for button %d",
          self->ptr_x, self->ptr_y, self->mouse_button);
    }

    if (self->ptr_event_mask & POINTER_EVENT_BUTTON_RELEASE) {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_mouse_button_release (self->mouse_button,
              self->ptr_x, self->ptr_y));
      GST_DEBUG ("sent mouse-button-release event at %f,%f for button %d",
          self->ptr_x, self->ptr_y, self->mouse_button);
    }

    if (self->ptr_event_mask & (POINTER_EVENT_AXIS_X | POINTER_EVENT_AXIS_Y)) {
      double scroll_x = 0;
      double scroll_y = 0;

      if (self->ptr_event_mask & POINTER_EVENT_AXIS_X)
        scroll_x = self->scroll_x;
      if (self->ptr_event_mask & POINTER_EVENT_AXIS_Y)
        scroll_y = self->scroll_y;

      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_mouse_scroll (self->ptr_x, self->ptr_y,
              scroll_x, scroll_y));
      GST_DEBUG ("sent mouse-scroll event at %f,%f with %f,%f", self->ptr_x,
          self->ptr_y, scroll_x, scroll_y);
    }
  }

  self->ptr_event_mask = 0;
  self->scroll_x = 0;
  self->scroll_y = 0;
}

static void
kb_keymap (void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd,
    uint32_t size)
{
  GstWlSeat *self = data;
  void *shm;

  GST_DEBUG ("received keyboard-keymap event");

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    GST_ERROR ("received keymap in unsupported format");
    return;
  }

  shm = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (shm == MAP_FAILED) {
    GST_ERROR ("failed to create mapping for xkb keymap");
    return;
  }

  xkb_keymap_unref (self->keymap);
  self->keymap = xkb_keymap_new_from_string (self->xkb, shm, format,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap (shm, size);
  close (fd);

  xkb_state_unref (self->kb_state);
  self->kb_state = xkb_state_new (self->keymap);
  if (self->kb_state == NULL) {
    GST_ERROR ("failed to initialize xkb state structure");
    xkb_keymap_unref (self->keymap);
  }
}

static void
kb_enter (void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface, struct wl_array *keys)
{
  GstWlSeat *self = data;
  xkb_keycode_t *key_code;
  xkb_keysym_t key_sym;
  char key_name[KEY_NAME_SIZE];

  GST_DEBUG ("received keyboard-enter event");

  g_array_append_vals (self->held_keys, keys->data, keys->size);
  if (self->navigation == NULL || self->kb_state == NULL)
    return;

  /* send key-press event for each already held key */
  wl_array_for_each (key_code, keys) {
    /* this is an evdev scancode, add 8 to get the XKB scancode */
    key_sym = xkb_state_key_get_one_sym (self->kb_state, *key_code + 8);

    if (xkb_keysym_get_name (key_sym, key_name, KEY_NAME_SIZE)) {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_key_press (key_name));
      GST_DEBUG ("sent key-press event for key \"%s\"", key_name);
    }
  }
}

static void
kb_leave (void *data, struct wl_keyboard *kb, uint32_t serial,
    struct wl_surface *surface)
{
  GstWlSeat *self = data;

  GST_DEBUG ("received keyboard-leave event");

  /* send key-release event for each still held key */
  if (self->navigation != NULL && self->kb_state != NULL) {
    xkb_keycode_t key_code;
    xkb_keysym_t key_sym;
    char key_name[KEY_NAME_SIZE];
    guint i;

    for (i = 0; i < self->held_keys->len; i++) {
      key_code = g_array_index (self->held_keys, xkb_keycode_t, i);
      key_sym = xkb_state_key_get_one_sym (self->kb_state, key_code);

      if (xkb_keysym_get_name (key_sym, key_name, KEY_NAME_SIZE) > 0) {
        gst_navigation_send_event_simple (self->navigation,
            gst_navigation_event_new_key_release (key_name));
        GST_DEBUG ("sent key-release event for key \"%s\"", key_name);
      }
    }
  }

  /* remove all held keys */
  g_array_set_size (self->held_keys, 0);
}

static void
kb_key (void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
    xkb_keycode_t key_code, uint32_t state)
{
  GstWlSeat *self = data;
  xkb_keysym_t key_sym;
  char key_name[KEY_NAME_SIZE];

  GST_DEBUG ("received keyboard-key event for key %u with state %u", key_code,
      state);

  /* this is an evdev scancode, add 8 to get the XKB scancode */
  key_code += 8;

  /* update held_keys array */
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    self->held_keys = g_array_append_val (self->held_keys, key_code);
  } else {
    for (guint i = 0; i < self->held_keys->len; i++) {
      if (g_array_index (self->held_keys, xkb_keycode_t, i) == key_code) {
        self->held_keys = g_array_remove_index_fast (self->held_keys, i);
        break;
      }
    }
  }

  if (self->navigation != NULL && self->kb_state != NULL) {
    key_sym = xkb_state_key_get_one_sym (self->kb_state, key_code);
    if (xkb_keysym_get_name (key_sym, key_name, KEY_NAME_SIZE) < 0)
      return;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_key_press (key_name));
      GST_DEBUG ("sent key-press event for key \"%s\"", key_name);
    } else {
      gst_navigation_send_event_simple (self->navigation,
          gst_navigation_event_new_key_release (key_name));
      GST_DEBUG ("sent key-release event for key \"%s\"", key_name);
    }
  }
}

static void
kb_modifiers (void *data, struct wl_keyboard *kb, uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
    uint32_t group)
{
  GstWlSeat *self = data;

  GST_DEBUG ("received keyboard-modifiers event");

  xkb_state_update_mask (self->kb_state,
      mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void
kb_repeat_info (void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay)
{
  /* ignore key-repeat settings */
  GST_DEBUG ("received keyboard-repeat-info event");
}

static void
touch_down (void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
    struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
  GstWlSeat *self = data;
  TouchPoint point;

  point.id = (guint) id;
  point.type = TOUCH_EVENT_DOWN;
  point.x = wl_fixed_to_double (x);
  point.y = wl_fixed_to_double (y);
  point.pressure = NAN;
  g_array_append_val (self->active_points, point);

  GST_DEBUG ("received touch-down event for id %d at %f,%f", id, point.x,
      point.y);
}

static void
touch_up (void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
    int32_t id)
{
  GstWlSeat *self = data;
  TouchPoint point;
  gint i;

  /* send touch-up events immediately, since wayland doesn't seem to send */
  /* touch-frame events if only a touch-up occured */
  for (i = 0; i < self->active_points->len; i++) {
    point = g_array_index (self->active_points, TouchPoint, i);

    if ((int32_t) point.id == id) {
      GST_DEBUG ("received touch-up event for id %d", id);
      self->active_points = g_array_remove_index_fast (self->active_points, i);

      if (self->navigation) {
        gst_navigation_send_event_simple (self->navigation,
            gst_navigation_event_new_touch_up (point.id, point.x, point.y));
        GST_DEBUG ("sent touch-up event for id %d at %f,%f", point.id,
            point.x, point.y);
      }

      return;
    }
  }

  GST_WARNING ("received touch-up event for unknown id %d", id);
}

static void
touch_motion (void *data, struct wl_touch *touch, uint32_t time, int32_t id,
    wl_fixed_t x, wl_fixed_t y)
{
  GstWlSeat *self = data;
  TouchPoint point;
  gint i;

  for (i = 0; i < self->active_points->len; i++) {
    point = g_array_index (self->active_points, TouchPoint, i);

    if ((int32_t) point.id == id) {
      point.x = wl_fixed_to_double (x);
      point.y = wl_fixed_to_double (y);
      if (point.type == TOUCH_EVENT_NONE)
        point.type = TOUCH_EVENT_MOTION;
      g_array_index (self->active_points, TouchPoint, i) = point;

      GST_DEBUG ("received touch-motion event for id %d at %f,%f", id, point.x,
          point.y);
      return;
    }
  }

  GST_WARNING ("received touch-motion event for unknown id %d", id);
}

static void
touch_shape (void *data, struct wl_touch *touch, int32_t id, wl_fixed_t major,
    wl_fixed_t minor)
{
  /* ignore touch shapes */
  GST_DEBUG ("received touch-shape event for id %d", id);
}

static void
touch_orientation (void *data, struct wl_touch *touch, int32_t id,
    wl_fixed_t orientation)
{
  /* ignore touch orientation too */
  GST_DEBUG ("received touch-orientation event for id %d", id);
}

static void
touch_cancel (void *data, struct wl_touch *touch)
{
  GstWlSeat *self = data;

  GST_DEBUG ("received touch-cancel event");

  /* clear the array */
  g_array_set_size (self->active_points, 0);

  if (self->navigation) {
    gst_navigation_send_event_simple (self->navigation,
        gst_navigation_event_new_touch_cancel ());
    GST_DEBUG ("sent touch-cancel event");
  }
}

static void
touch_frame (void *data, struct wl_touch *touch)
{
  GstWlSeat *self = data;
  TouchPoint point;
  gint i;

  GST_DEBUG ("received touch-frame event");

  for (i = 0; i < self->active_points->len; i++) {
    point = g_array_index (self->active_points, TouchPoint, i);

    if (self->navigation) {
      switch (point.type) {
        case TOUCH_EVENT_NONE:
          break;

        case TOUCH_EVENT_DOWN:
          gst_navigation_send_event_simple (self->navigation,
              gst_navigation_event_new_touch_down (point.id, point.x, point.y,
                  point.pressure));
          GST_DEBUG ("sent touch-down event for id %u at %f,%f", point.id,
              point.x, point.y);
          break;

        case TOUCH_EVENT_MOTION:
          gst_navigation_send_event_simple (self->navigation,
              gst_navigation_event_new_touch_motion (point.id, point.x, point.y,
                  point.pressure));
          GST_DEBUG ("sent touch-motion event for id %u at %f,%f", point.id,
              point.x, point.y);
          break;
      }
    }
    point.type = TOUCH_EVENT_NONE;
    g_array_index (self->active_points, TouchPoint, i) = point;
  }
}
