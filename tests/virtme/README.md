## High level description of the files in this directory.

### virtme-run.sh

A helper script that uses 'virtme' to launch a qemu virtual machine with the
host filesystem exposed inside the virtual machine.

### run-virt-test.sh

Run the given command and retrieve the command status in the given status file.
This is necessary because virtme doesn't return the exit code of the command.

### meson.build

Contains one rule for meson test cases that launches tests inside virtual
machines.

### gen-visl-reference.sh

Used to re-generate the fluster reference with visl. When a change happens in
the visl kernel driver, this script can be used as a replacement of
`virtme-run.sh` in `meson.build` so that running the `v4l2-stateless-decoders`
test suite will update the fluster references stored in
`ci/fluster/visl_references/test_suites/`.
