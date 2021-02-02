/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32utils.h"
#include <string.h>

struct _GstWin32File
{
  HANDLE file_handle;

  /* For unbuffered I/O */
  guint8 *buffer;
  gint64 buffer_size;

  gint64 read_size;
  gint64 remaining;
};

GstWin32File *
gst_win32_file_open (const gchar * filename, gint desired_access,
    gint share_mode, SECURITY_ATTRIBUTES * security_attr,
    gint creation_disposition, gint file_flags, gint file_attr,
    gint security_qos_flags, HANDLE template_file)
{
  HANDLE file_handle = INVALID_HANDLE_VALUE;
  wchar_t *wfilename;
  GstWin32File *self;

  g_return_val_if_fail (filename, NULL);

  wfilename = g_utf8_to_utf16 (filename, -1, NULL, NULL, NULL);
  if (!wfilename)
    return NULL;

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  /* CreateFileW is desktop only API. We can use CreateFile2 instead
   * which can be used for both desktop and UWP. But it requires Windows 8 */
  {
    CREATEFILE2_EXTENDED_PARAMETERS params;
    params.dwSize = sizeof (CREATEFILE2_EXTENDED_PARAMETERS);
    params.dwFileAttributes = file_attr;
    params.dwFileFlags = file_flags;
    params.dwSecurityQosFlags = security_qos_flags;
    params.lpSecurityAttributes = security_attr;
    params.hTemplateFile = template_file;

    file_handle = CreateFile2 (wfilename, desired_access, share_mode,
        creation_disposition, &params);
  }
#else
  file_handle = CreateFileW (wfilename, desired_access, share_mode,
      security_attr, creation_disposition,
      file_flags | file_attr | security_qos_flags, template_file);
#endif
  if (file_handle == INVALID_HANDLE_VALUE)
    return NULL;

  self = g_new0 (GstWin32File, 1);
  self->file_handle = file_handle;

  /* NOTE: unbuffed read has some restrictions.
   * See https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering */
  if ((file_flags & FILE_FLAG_NO_BUFFERING) == FILE_FLAG_NO_BUFFERING) {
    SYSTEM_INFO system_info;
    guint8 *buffer;

    /* Get page size and allocate buffer for unbuffered I/O */
    GetNativeSystemInfo (&system_info);

    g_assert (system_info.dwPageSize != 0);

    /* Allocate page aligned memory for direct I/O */
    /* FIXME: do we want to larger size buffer (multiple of page size)? */
    buffer = _aligned_malloc (system_info.dwPageSize, system_info.dwPageSize);

    if (!buffer) {
      gst_win32_file_close (self);
      return NULL;
    }

    self->buffer = buffer;
    self->buffer_size = system_info.dwPageSize;
  }

  return self;
}

void
gst_win32_file_close (GstWin32File * file)
{
  g_return_if_fail (file != NULL);

  if (file->file_handle != INVALID_HANDLE_VALUE)
    CloseHandle (file->file_handle);

  if (file->buffer)
    _aligned_free (file->buffer);

  g_free (file);
}

HANDLE
gst_win32_file_get_file_handle (GstWin32File * file)
{
  g_return_val_if_fail (file != NULL, INVALID_HANDLE_VALUE);

  return file->file_handle;
}

static gint64
gst_win32_file_calculate_unbuffered_file_begin_seek_offset (GstWin32File * file,
    gint64 offset, gint move_method, gint64 * logical_position)
{
  LARGE_INTEGER val;
  gint64 new_offset;
  DWORD last_err;

  *logical_position = -1;

  if (!file->buffer)
    return offset;

  switch (move_method) {
    case FILE_BEGIN:
      *logical_position = offset;

      return GST_ROUND_DOWN_N (offset, file->buffer_size);
    case FILE_CURRENT:
      /* Get current position */
      val.QuadPart = 0;
      val.LowPart = SetFilePointer (file->file_handle, val.LowPart,
          &val.HighPart, move_method);

      /* ERROR */
      last_err = GetLastError ();
      if (val.LowPart == INVALID_SET_FILE_POINTER && last_err != NO_ERROR) {
        GST_ERROR ("Couldn't get current position, last error %d", last_err);
        return -1;
      }

      new_offset = val.QuadPart + offset;
      if (new_offset < 0) {
        GST_ERROR ("New offset is negative %" G_GINT64_FORMAT, new_offset);
        return -1;
      }

      *logical_position = new_offset;

      return GST_ROUND_DOWN_N (new_offset, file->buffer_size);
    case FILE_END:
      if (offset > 0) {
        GST_ERROR ("Invalid offset for FILE_END %" G_GINT64_FORMAT, offset);
        return -1;
      }

      /* The position of FILE_END might not be sector aligned */
      if (!GetFileSizeEx (file->file_handle, &val)) {
        GST_ERROR ("Couldn't get query file size, last error %d",
            GetLastError ());
        return -1;
      }

      new_offset = val.QuadPart + offset;
      if (new_offset < 0) {
        GST_ERROR ("New offset is negative %" G_GINT64_FORMAT, new_offset);
        return -1;
      }

      *logical_position = new_offset;

      return GST_ROUND_DOWN_N (new_offset, file->buffer_size);
    default:
      g_assert_not_reached ();
      return -1;
  }
}

gint64
gst_win32_file_seek (GstWin32File * file, gint64 offset, gint move_method)
{
  LARGE_INTEGER val;
  gint64 logical_pos = 0;

  g_return_val_if_fail (file != NULL, -1);
  g_return_val_if_fail (file->file_handle != INVALID_HANDLE_VALUE, -1);

  /* Seek position for unbuffered I/O needs to be sector-aligned.
   * Calculating new offset in that case */
  if (file->buffer) {
    gint64 new_offset =
        gst_win32_file_calculate_unbuffered_file_begin_seek_offset (file,
        offset, move_method, &logical_pos);

    if (new_offset < 0)
      return -1;

    move_method = FILE_BEGIN;
    offset = new_offset;
  }

  val.QuadPart = offset;
  val.LowPart = SetFilePointer (file->file_handle, val.LowPart, &val.HighPart,
      move_method);

  if (val.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR) {
    val.QuadPart = -1;
  } else if (file->buffer) {
    gint64 to_drop = logical_pos - offset;

    /* Return logical position, otherwise caller would be confused */
    val.QuadPart = logical_pos;

    g_assert (logical_pos >= offset);
    g_assert (to_drop < file->buffer_size);

    file->read_size = file->remaining = 0;

    /* If logical position is different from actual position,
     * pre-load buffer and mark a position where we need to read from */
    if (to_drop > 0) {
      DWORD read_size = 0;
      BOOL ret;

      ret = ReadFile (file->file_handle, file->buffer, file->buffer_size,
          &read_size, NULL);

      if (!ret) {
        GST_ERROR ("Failed to pre-load buffer, last error %d", GetLastError ());
        return -1;
      }

      /* Possible? */
      if (to_drop > read_size) {
        GST_ERROR ("Need %" G_GINT64_FORMAT " bytes but read only %d",
            to_drop, read_size);
        return -1;
      }

      file->read_size = read_size;
      file->remaining = read_size - to_drop;
    } else {
      file->read_size = file->remaining = 0;
    }
  }

  return (guint64) val.QuadPart;
}

gint
gst_win32_file_read (GstWin32File * file, gpointer buf, gint count)
{
  BOOL ret;
  DWORD read_size = 0;

  g_return_val_if_fail (file != NULL, -1);
  g_return_val_if_fail (file->file_handle != INVALID_HANDLE_VALUE, -1);
  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (count >= 0, -1);

  if (file->buffer) {
    gint to_copy = 0;
    gint64 offset;

    /* Return data if we have buffered one. If it's insufficent,
     * caller will call this method again */
    if (file->remaining) {
      to_copy = MIN (count, file->remaining);
      offset = file->read_size - file->remaining;

      memcpy (buf, file->buffer + offset, to_copy);
      file->remaining -= to_copy;
      return to_copy;
    }

    ret = ReadFile (file->file_handle, file->buffer, file->buffer_size,
        &read_size, NULL);
    if (!ret) {
      GST_ERROR ("Read failed, last error %d", GetLastError ());
      return -1;
    }

    file->read_size = file->remaining = read_size;
    to_copy = MIN (count, file->remaining);

    memcpy (buf, file->buffer, to_copy);
    file->remaining -= to_copy;
    return to_copy;
  } else {
    ret = ReadFile (file->file_handle, buf, count, &read_size, NULL);
    if (!ret)
      return -1;

    return read_size;
  }
}
