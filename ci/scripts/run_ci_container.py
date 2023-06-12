#! /usr/bin/env python3

import argparse
import distro
import os
import subprocess

from ruamel.yaml import YAML


def get_fedora_tag() -> str:
    filename = ".gitlab-image-tags.yml"
    yaml = YAML(typ="safe")
    with open(filename) as f:
        tags = yaml.load(f)
        return tags["variables"]["FEDORA_TAG"]


def get_fedora_image() -> str:
    base = "registry.freedesktop.org/gstreamer/gstreamer/amd64"
    tag = get_fedora_tag()
    return f"{base}/{tag}"


def get_volume_string() -> str:
    volume = f"{os.getcwd()}:/app/gstreamer"

    # On SELinux we need to set the proper label for the volume
    # https://docs.docker.com/storage/bind-mounts/#configure-the-selinux-label
    if "fedora" in distro.id():
        volume += ":z"

    return volume


def docker(*args, **kwargs):
    subprocess.run(["docker"] + list(args), check=True, **kwargs)


def docker_exists(*args) -> bool:
    res = subprocess.run(
        ["docker", "inspect"] + list(args),
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode

    # if its positive that means that we got an
    # error code back and the object does not exist,
    # so invert the boolean before returning
    return not bool(res)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--image",
        help="Specify the OCI or docker image to use",
    )
    args = parser.parse_args()

    if args.image:
        image = args.image
    else:
        image = get_fedora_image()

    container_name = "gstreamer-ci"

    if not docker_exists(image):
        print("Pulling CI image")
        docker("pull", image)

    if not docker_exists(container_name):
        print("Creating container")
        volume = get_volume_string()

        # FIXME: id mapping doesn't work yet with our setup
        # --user="$(id --user):$(id --group)"
        docker(
            "create",
            "-it",
            f"--name={container_name}",
            f"--volume={volume}",
            "--workdir=/app/gstreamer",
            image,
            "bash",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    else:
        print("Reusing existing container")

    docker("start", "--attach", "--interactive", container_name)
