/* GStreamer
 * Copyright (C) 2024 Alexander Slobodeniuk <aslobodeniuk@fluendo.com>
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

#ifndef __GST_WIN32_RW_LOCK__
#define __GST_WIN32_RW_LOCK__

#include <windows.h>

G_BEGIN_DECLS

/* This is an implementation of the GRWLock without AcquireSRWLockShared,
 * that is needed because of the Windows OS bug:
 * https://github.com/microsoft/STL/issues/4448
 */

typedef enum {
  GST_WIN32_RW_LOCK_NULL = 0,
  GST_WIN32_RW_LOCK_PREPARING,
  GST_WIN32_RW_LOCK_READY
} GstWin32RWLockState;

typedef struct
{
  /* Used for lazy initialization */
  GstWin32RWLockState state;

  CRITICAL_SECTION priv_lock;
  CONDITION_VARIABLE barrier;
  gint read_locks;
  gint write_locks;

  /* This is a micro-performance hack to avoid read_unlock
   * waking condvar all the time. When the GstWin32RWLock
   * is not writing it only locks/unlocks it's mutex and
   * checks or changes some integer values. */
  gint pending_writes;
} GstWin32RWLock;

#define GST_WIN32_RW_LOCK_INIT { GST_WIN32_RW_LOCK_NULL }

static void
gst_win32_rw_lock_init (GstWin32RWLock *self)
{
  InitializeCriticalSection (&self->priv_lock);
  InitializeConditionVariable (&self->barrier);
  self->read_locks = 0;
  self->write_locks = 0;
  self->pending_writes = 0;
  self->state = GST_WIN32_RW_LOCK_READY;
}

static void
gst_win32_rw_lock_lazy_init (GstWin32RWLock *self)
{
  GstWin32RWLockState current_state;

  if (G_LIKELY (self->state == GST_WIN32_RW_LOCK_READY))
    return;

  G_STATIC_ASSERT (sizeof (GstWin32RWLockState) == sizeof (gint));

  current_state = (GstWin32RWLockState)
    InterlockedCompareExchange ((volatile guint *)&self->state,
    GST_WIN32_RW_LOCK_PREPARING,
    GST_WIN32_RW_LOCK_NULL);

  /* If the current state is NULL - switch it to PREPARING first,
   * then perform the initialization and set it to READY.
   *
   * If the state is PREPARING - spin until it befomes READY.
   *
   * If the state is READY - do nothing, lock is already initialized
   */

  switch (current_state) {
  case GST_WIN32_RW_LOCK_READY:
    return;
  case GST_WIN32_RW_LOCK_NULL:
    /* We are the first thread that touches the lock: doing the init */
    gst_win32_rw_lock_init (self);
    self->state = GST_WIN32_RW_LOCK_READY;
    MemoryBarrier ();
    return;
  case GST_WIN32_RW_LOCK_PREPARING:
    /* The init proccess is going on right now in another thread,
     * let it finish */
    MemoryBarrier ();
    while (self->state != GST_WIN32_RW_LOCK_READY) {
      Sleep(0);
      MemoryBarrier ();
    }
    return;
  default:
    g_error ("Memory have been corrupted");
  }
}

static void
gst_win32_rw_lock_clear (GstWin32RWLock *self)
{
  DeleteCriticalSection (&self->priv_lock);
  self->state = GST_WIN32_RW_LOCK_NULL;
}

static void
gst_win32_rw_lock_writer_lock (GstWin32RWLock *self)
{
  gst_win32_rw_lock_lazy_init (self);

  EnterCriticalSection (&self->priv_lock);

  self->pending_writes ++;

  /* Wait until there're no write nor read locks. */
  while (self->write_locks || self->read_locks) {
    SleepConditionVariableCS (&self->barrier,
       &self->priv_lock, INFINITE);
  }

  /* Here we are garanteed to have 0 read and 0 write locks
   * Set write locks to 1. */
  self->write_locks = 1;
  self->pending_writes --;

  LeaveCriticalSection (&self->priv_lock);
}

static void
gst_win32_rw_lock_writer_unlock (GstWin32RWLock *self)
{
  EnterCriticalSection (&self->priv_lock);

  self->write_locks --;
  /* Notify read and write locks. It's important to wake all of them
   * because there might be many read locks waiting. */
  WakeAllConditionVariable (&self->barrier);

  LeaveCriticalSection (&self->priv_lock);
}

static void
gst_win32_rw_lock_reader_lock (GstWin32RWLock *self)
{
  gst_win32_rw_lock_lazy_init (self);

  EnterCriticalSection (&self->priv_lock);

  /* If locked for writing - wait */
  while (self->write_locks) {
    SleepConditionVariableCS (&self->barrier,
        &self->priv_lock, INFINITE);
  }

  /* Here we are garanteed to have 0 write locks
   * Increase amount of read locks. */
  self->read_locks ++;

  LeaveCriticalSection (&self->priv_lock);
}

static void
gst_win32_rw_lock_reader_unlock (GstWin32RWLock *self)
{
  EnterCriticalSection (&self->priv_lock);

  self->read_locks --;

  /* Possibly unlock for writing.
   * It makes sence to wake only one thread, because we are
   * sure there're only write locks might be waiting, and
   * write locks have to wait for each other anyway. */
  if (self->pending_writes) {
    WakeConditionVariable (&self->barrier);
  }

  LeaveCriticalSection (&self->priv_lock);
}

G_END_DECLS

#ifdef __cplusplus
class GstWin32RWLockReaderGuard
{
public:
  explicit GstWin32RWLockReaderGuard(GstWin32RWLock * lock) : lock_ (lock)
  {
    gst_win32_rw_lock_reader_lock (lock_);
  }

  ~GstWin32RWLockReaderGuard()
  {
    gst_win32_rw_lock_reader_unlock (lock_);
  }

  GstWin32RWLockReaderGuard(const GstWin32RWLockReaderGuard&) = delete;
  GstWin32RWLockReaderGuard& operator=(const GstWin32RWLockReaderGuard&) = delete;

private:
  GstWin32RWLock *lock_;
};

class GstWin32RWLockWriterGuard
{
public:
  explicit GstWin32RWLockWriterGuard(GstWin32RWLock * lock) : lock_ (lock)
  {
    gst_win32_rw_lock_writer_lock (lock_);
  }

  ~GstWin32RWLockWriterGuard()
  {
    gst_win32_rw_lock_writer_unlock (lock_);
  }

  GstWin32RWLockWriterGuard(const GstWin32RWLockWriterGuard&) = delete;
  GstWin32RWLockWriterGuard& operator=(const GstWin32RWLockWriterGuard&) = delete;

private:
  GstWin32RWLock *lock_;
};
#endif

#endif /* __GST_WIN32_RW_UNLOCK__ */
