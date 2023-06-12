GStreamer Continuous Integration
================================

This repository contains all material relevant to the GStreamer
Continuous Integration system.

* Docker images

* Build scripts and code

Basic instructions for reproducing CI issues locally
====================================================

Note the URL of the image in the job logs, for instance:

```
Using docker image sha256:ac097589af0f486321adf7e512f2237c55533b9b08dabb49164a521a374d406d for registry.freedesktop.org/ocrete/gstreamer/amd64/fedora:2022-12-10.0-main with digest registry.freedesktop.org/ocrete/gstreamer/amd64/fedora@sha256:a2f7be944964a115ada2b3675c190bc9a094a5b35eba64a1ac38d52d55d13663
```

Pull the image:

```
docker pull registry.freedesktop.org/ocrete/gstreamer/amd64/fedora:2022-12-10.0-main
```

Run it:

This will bind-mount the current directory into the container so you can use your locale repository inside the container.

```
docker run -it --volume "$(pwd):/app/gstreamer" --workdir "/app/gstreamer" fedora:2022-12-10.0-main
```

  > If you are using SELinux, you should to add `:z` at the end of the volume path to set the label `--volume "$(pwd):/app/gstreamer:z"`. See the docker [documentation](https://docs.docker.com/storage/bind-mounts/#configure-the-selinux-label) for more.


Adapt the above to your situation.

Now, export the relevant variables by observing the job logs and `.gitlab-ci.yml` at
the root of the GStreamer repository, then run the steps listed in the script section.
