# Qt Waylandsink Example

Example of using `waylandsink` in Qt

## How to build

```shell
$ mkdir build && cd build
$ cmake path/to/this/example
$ make
```

## How to run

Play `videotestsrc`:
```shell
$ QT_QPA_PLATFORM=wayland ./waylandsink
```

Play a video:
```shell
$ QT_QPA_PLATFORM=wayland ./waylandsink file:///home/user/Videos/video.mp4
```
