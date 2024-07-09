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

#ifndef WAYLANDSINK_VIDEOWIDGET_H
#define WAYLANDSINK_VIDEOWIDGET_H

#include <QWidget>

#include <gst/video/videooverlay.h>

class VideoWidget : public QWidget {
Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setPipeline(const QString &pipelineStr);
    void setVideoOverlay(GstVideoOverlay *videoOverlay);

public slots:
    void play();
    void pause();
    void stop();

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool _firstActive = true;
    GstElement *_pipeline = nullptr;
    GstVideoOverlay *_videoOverlay = nullptr;

    void _setState(GstState state);
};


#endif //WAYLANDSINK_VIDEOWIDGET_H
