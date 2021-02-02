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

struct _GstWin32File
{
  HANDLE file_handle;
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

  return self;
}

void
gst_win32_file_close (GstWin32File * file)
{
  g_return_if_fail (file != NULL);

  if (file->file_handle != INVALID_HANDLE_VALUE)
    CloseHandle (file->file_handle);

  g_free (file);
}

HANDLE
gst_win32_file_get_file_handle (GstWin32File * file)
{
  g_return_val_if_fail (file != NULL, INVALID_HANDLE_VALUE);

  return file->file_handle;
}

gint64
gst_win32_file_seek (GstWin32File * file, gint64 offset, gint move_method)
{
  LARGE_INTEGER val;

  g_return_val_if_fail (file != NULL, -1);
  g_return_val_if_fail (file->file_handle != INVALID_HANDLE_VALUE, -1);

  val.QuadPart = offset;
  val.LowPart = SetFilePointer (file->file_handle, val.LowPart, &val.HighPart,
      move_method);

  if (val.LowPart == INVALID_SET_FILE_POINTER && GetLastError () != NO_ERROR)
    val.QuadPart = -1;

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

  ret = ReadFile (file->file_handle, buf, count, &read_size, NULL);
  if (!ret)
    return -1;

  return read_size;
}
