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

#include <QApplication>
#include <QtCore/QCommandLineParser>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    gst_init(&argc, &argv);

    VideoWidget v;
    v.resize(500, 500);

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt Waylandsink Example");
    parser.addHelpOption();
    parser.addPositionalArgument("video", "Video path, eg. file:///home/user/Videos/video.mp4");
    parser.process(app);

    /* Default pipeline to run if no video path is passed. */
    QString pipelineStr = QString("videotestsrc ! video/x-raw,width=%1,height=%2 ! waylandsink")
        .arg(v.width()).arg(v.height());

    if (!parser.positionalArguments().isEmpty())
        pipelineStr = "playbin video-sink=waylandsink uri=" + parser.positionalArguments().constFirst();

    v.setPipeline(pipelineStr);
    v.show();

    return app.exec();
}
