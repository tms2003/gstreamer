/* Gstreamer
 * Copyright (C) <2024> Alexander Slobodeniuk <aslobodeniuk@fluendo.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>
#include <gst/d3d11/gstwin32rwlock.h>

static struct
{
  GstWin32RWLock lock;
  gint total_readers_counted;
  gint total_writers_counted;
  gpointer thread_cookie;
  gboolean stopping;
} fixture;

static void
fixture_init (gboolean init_lock)
{
  if (init_lock)                // sometimes we want to check locks lazy init
    gst_win32_rw_lock_init (&fixture.lock);
  fixture.total_readers_counted = 0;
  fixture.total_writers_counted = 0;
  fixture.thread_cookie = NULL;
  fixture.stopping = FALSE;
}

static void
fixture_clear (void)
{
  gst_win32_rw_lock_clear (&fixture.lock);
}

#define TOTAL_READERS 32
#define ITERATIONS_PER_TEST 100

static gpointer
exclusive_access_read_thread (gpointer data)
{
  while (!g_atomic_int_get (&fixture.stopping)) {
    gint64 time = g_get_monotonic_time ();
    time += 1000;               // 1ms

    gst_win32_rw_lock_reader_lock (&fixture.lock);
    gint readers = g_atomic_int_get (&fixture.total_readers_counted);
    g_atomic_int_inc (&fixture.total_readers_counted);
    while (time > g_get_monotonic_time ()) {
      g_usleep (1);
      g_atomic_pointer_set (&fixture.thread_cookie, g_thread_self ());
    }

    // while the reader is locked other readers can enter
    fail_unless (g_atomic_int_get (&fixture.total_readers_counted) > readers);

    gst_win32_rw_lock_reader_unlock (&fixture.lock);

    g_usleep (5);
  }

  return NULL;
}

static gpointer
exclusive_access_write_thread (gpointer data)
{
  while (!g_atomic_int_get (&fixture.stopping)) {
    gint total_writes;

    gst_win32_rw_lock_writer_lock (&fixture.lock);
    // reset readers counter. This allows to check that we are waiting for reader to unlock
    g_atomic_int_set (&fixture.total_readers_counted, 0);

    g_atomic_int_inc (&fixture.total_writers_counted);
    total_writes = fixture.total_writers_counted;

    // set our thread as cookie
    g_atomic_pointer_set (&fixture.thread_cookie, g_thread_self ());
    // sleep 2 ms
    g_usleep (2 * 1000);

    // No way anyone - reading or writing can spoil our cookie
    assert_equals_pointer (g_thread_self (),
        g_atomic_pointer_get (&fixture.thread_cookie));
    assert_equals_int (fixture.total_readers_counted, 0);
    // writes haven't changed
    assert_equals_int (fixture.total_writers_counted, total_writes);

    gst_win32_rw_lock_writer_unlock (&fixture.lock);

    g_usleep (10);
  }

  return NULL;
}

static void
exclusive_access_iteration (void)
{
  GThread *threads[TOTAL_READERS];
  gint t;

  fixture.stopping = FALSE;

  for (t = 0; t < TOTAL_READERS; t++) {
    threads[t] = g_thread_new (NULL,
        (t % 2) ? exclusive_access_read_thread : exclusive_access_write_thread,
        NULL);
  }

  g_usleep (1000 * 50);
  fixture.stopping = TRUE;

  for (t = 0; t < TOTAL_READERS; t++)
    g_thread_join (threads[t]);

  fail_unless (fixture.total_writers_counted > 0);
}

GST_START_TEST (test_win32rwlock_exclusive_access)
{
  gint i;

  for (i = 0; i < ITERATIONS_PER_TEST / 2; i++) {
    GST_INFO ("Iteration %d", i);
    fixture_init (i % 2);
    exclusive_access_iteration ();
    fixture_clear ();
    GST_INFO ("Passed");
  }
}

GST_END_TEST;


static gpointer
reading_freedom_thread (gpointer data)
{
  gst_win32_rw_lock_reader_lock (&fixture.lock);
  g_atomic_int_inc (&fixture.total_readers_counted);

  // sleep 100 ms - plenty of time.
  // so we expect that all the readers could enter during this time
  g_usleep (1000 * 100);

  assert_equals_int (fixture.total_readers_counted, TOTAL_READERS);

  gst_win32_rw_lock_reader_unlock (&fixture.lock);

  return NULL;
}

static void
reading_freedom_iteration (void)
{
  GThread *threads[TOTAL_READERS];
  gint t;

  fixture.total_readers_counted = 0;

  for (t = 0; t < TOTAL_READERS; t++)
    threads[t] = g_thread_new (NULL, reading_freedom_thread, NULL);

  for (t = 0; t < TOTAL_READERS; t++)
    g_thread_join (threads[t]);
}

GST_START_TEST (test_win32rwlock_reading_freedom)
{
  gint i;

  for (i = 0; i < ITERATIONS_PER_TEST; i++) {
    GST_INFO ("Iteration %d", i);
    fixture_init (i % 2);
    reading_freedom_iteration ();
    fixture_clear ();
    GST_INFO ("Passed");
  }
}

GST_END_TEST;

static int lock_min_diff;
static int lock_max_diff;
static int unlock_min_diff;
static int unlock_max_diff;

static gpointer
reader_speed_thread (gpointer data)
{
  gint diff;
  gint64 time = g_get_monotonic_time ();
  gst_win32_rw_lock_reader_lock (&fixture.lock);
  diff = g_get_monotonic_time () - time;

  if (diff > g_atomic_int_get (&lock_max_diff))
    g_atomic_int_set (&lock_max_diff, diff);

  if (diff < g_atomic_int_get (&lock_min_diff))
    g_atomic_int_set (&lock_min_diff, diff);


  // Just sleep 1 ms
  g_usleep (1000 * 1);

  time = g_get_monotonic_time ();
  gst_win32_rw_lock_reader_unlock (&fixture.lock);
  diff = g_get_monotonic_time () - time;

  if (diff > g_atomic_int_get (&unlock_max_diff))
    g_atomic_int_set (&unlock_max_diff, diff);

  if (diff < g_atomic_int_get (&unlock_min_diff))
    g_atomic_int_set (&unlock_min_diff, diff);

  return NULL;
}

GST_START_TEST (test_win32rwlock_reader_lock_speed)
{
  GThread *threads[TOTAL_READERS];
  gint t;
  const gint TCOUNT = MIN (TOTAL_READERS, g_get_num_processors () - 1);

  fixture_init (TRUE);

  for (t = 0; t < TCOUNT; t++)
    threads[t] = g_thread_new (NULL, reader_speed_thread, NULL);

  g_usleep (1000 * 500);

  for (t = 0; t < TCOUNT; t++)
    g_thread_join (threads[t]);

  fixture_clear ();

  GST_INFO ("reader lock min diff = %d us", lock_min_diff);
  GST_INFO ("reader lock max diff = %d us", lock_max_diff);
  GST_INFO ("reader unlock min diff = %d us", unlock_min_diff);
  GST_INFO ("reader unlock max diff = %d us", unlock_max_diff);
}

GST_END_TEST;

static Suite *
win32rwlock_suite (void)
{
  Suite *s = suite_create ("GstWin32RWLock library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_win32rwlock_reading_freedom);
  tcase_add_test (tc_chain, test_win32rwlock_exclusive_access);
  tcase_add_test (tc_chain, test_win32rwlock_reader_lock_speed);

  return s;
}

GST_CHECK_MAIN (win32rwlock);
