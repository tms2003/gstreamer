/*
 * Copyright (C) 2022 Toshiba Corporation.
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

#include "videowidget.h"

#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#include <gst/wayland/wayland.h>

static gboolean busWatchCb(GstBus *bus, GstMessage *message, VideoWidget *data) {
    auto messageType = GST_MESSAGE_TYPE(message);
    if (messageType == GST_MESSAGE_ERROR) {
        GError *err = nullptr;
        gchar *dbg = nullptr;
        gst_message_parse_error(message, &err, &dbg);
        qDebug(" %s: %s", GST_OBJECT_NAME (message->src), err->message);
        qDebug("Debugging info: %s", (dbg) ? dbg : "none");
        g_error_free(err);
        g_free(dbg);
    }

    return TRUE;
}

GstBusSyncReply busSyncHandler(GstBus *bus, GstMessage *message, gpointer user_data) {
    if (gst_is_wayland_display_handle_need_context_message(message)) {
        GstContext *context;
        QPlatformNativeInterface *pni;
        struct wl_display *display_handle;

        pni = QGuiApplication::platformNativeInterface();
        display_handle = (struct wl_display *) pni->nativeResourceForWindow("display", nullptr);
        context = gst_wayland_display_handle_context_new(display_handle);
        gst_element_set_context(GST_ELEMENT (GST_MESSAGE_SRC(message)), context);
        gst_context_unref(context);

        goto drop;
    } else if (gst_is_video_overlay_prepare_window_handle_message(message)) {
        /* Qt application window needs to be ready at this time
         * or waylandsink will create a new window by itself. */
        auto *v = static_cast<VideoWidget *>(user_data);
        QPlatformNativeInterface *pni;
        struct wl_surface *window_handle;
        GstVideoOverlay *videoOverlay;

        videoOverlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC(message));
        pni = QGuiApplication::platformNativeInterface();

        /* Get window handle from widget's window
         *   https://doc.qt.io/qt-5/qwidget.html#window */
        window_handle = (struct wl_surface *) pni->nativeResourceForWindow("surface",
                                                                           v->window()->windowHandle());

        gst_video_overlay_set_window_handle(videoOverlay, (guintptr) window_handle);
        gst_video_overlay_set_render_rectangle(videoOverlay, v->x(), v->y(), v->width(), v->height());

        v->setVideoOverlay(videoOverlay);
        goto drop;
    }

    return GST_BUS_PASS;

    drop:
    gst_message_unref(message);
    return GST_BUS_DROP;
}

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent) {
    /* Make this widget expand, so it can fill empty space in a Qt layout. */
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

VideoWidget::~VideoWidget() {
    if (_pipeline) {
        stop();
        g_object_unref(_pipeline);
    }
    _pipeline = nullptr;
    _videoOverlay = nullptr;
}

void VideoWidget::setVideoOverlay(GstVideoOverlay *videoOverlay) {
    _videoOverlay = videoOverlay;
}

void VideoWidget::setPipeline(const QString &pipelineStr) {
    qDebug("Pipeline: %s", pipelineStr.toStdString().c_str());
    _pipeline = gst_parse_launch(pipelineStr.toStdString().c_str(), nullptr);
    if (!_pipeline)
        qFatal("Failed to create pipeline");

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE (_pipeline));
    gst_bus_set_sync_handler(bus, busSyncHandler, this, nullptr);
    gst_bus_add_watch(bus, reinterpret_cast<GstBusFunc>(&busWatchCb), this);
    gst_object_unref(bus);
}

void VideoWidget::play() {
    _setState(GST_STATE_PLAYING);
}

void VideoWidget::pause() {
    _setState(GST_STATE_PAUSED);
}

void VideoWidget::stop() {
    _setState(GST_STATE_NULL);
}

bool VideoWidget::event(QEvent *event) {
    /* Play video automatically at the first time window is active. */
    if (event->type() == QEvent::WindowActivate) {
        if (_firstActive) {
            play();
            _firstActive = false;
        }
    }
    return QWidget::event(event);
}

void VideoWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    /* Change render rectangle as widget size. */
    if (_videoOverlay)
        gst_video_overlay_set_render_rectangle(_videoOverlay, x(), y(), width(), height());
}

void VideoWidget::_setState(GstState state) {
    if (_pipeline)
        gst_element_set_state(_pipeline, state);
}