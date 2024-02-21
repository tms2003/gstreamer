#!/bin/bash
# shellcheck disable=SC1090
# shellcheck disable=SC1091
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC2155

# Second-stage init, used to set up devices and our job environment before
# running tests.

# Make sure to kill itself and all the children process from this script on
# exiting, since any console output may interfere with LAVA signals handling,
# which based on the log console.
cleanup() {
  if [ "$BACKGROUND_PIDS" = "" ]; then
    return 0
  fi

  set +x
  echo "Killing all child processes"
  for pid in $BACKGROUND_PIDS
  do
    kill "$pid" 2>/dev/null || true
  done

  # Sleep just a little to give enough time for subprocesses to be gracefully
  # killed. Then apply a SIGKILL if necessary.
  sleep 5
  for pid in $BACKGROUND_PIDS
  do
    kill -9 "$pid" 2>/dev/null || true
  done

  BACKGROUND_PIDS=
  set -x
}
trap cleanup INT TERM EXIT

# Space separated values with the PIDS of the processes started in the
# background by this script
BACKGROUND_PIDS=


for path in '/dut-env-vars.sh' '/set-job-env-vars.sh' './set-job-env-vars.sh'; do
    [ -f "$path" ] && source "$path"
done
. "$SCRIPTS_DIR"/setup-test-env.sh

set -ex

# Set up any devices required by the jobs
[ -z "$HWCI_KERNEL_MODULES" ] || {
    echo -n $HWCI_KERNEL_MODULES | xargs -d, -n1 /usr/sbin/modprobe
}

# Set up ZRAM
HWCI_ZRAM_SIZE=2G
if /sbin/zramctl --find --size $HWCI_ZRAM_SIZE -a zstd; then
    mkswap /dev/zram0
    swapon /dev/zram0
    echo "zram: $HWCI_ZRAM_SIZE activated"
else
    echo "zram: skipping, not supported"
fi

# Fix prefix confusion: the build installs to $CI_PROJECT_DIR, but we expect
# it in /install
ln -sf $CI_PROJECT_DIR/install /install
export LD_LIBRARY_PATH=/install/lib
export LIBGL_DRIVERS_PATH=/install/lib/dri

# Store Mesa's disk cache under /tmp, rather than sending it out over NFS.
export XDG_CACHE_HOME=/tmp

# Make sure Python can find all our imports
export PYTHONPATH=$(python3 -c "import sys;print(\":\".join(sys.path))")

# Start a little daemon to capture the first devcoredump we encounter.  (They
# expire after 5 minutes, so we poll for them).
if [ -x /capture-devcoredump.sh ]; then
  /capture-devcoredump.sh &
  BACKGROUND_PIDS="$! $BACKGROUND_PIDS"
fi

set +e
bash -c ". $SCRIPTS_DIR/setup-test-env.sh && $HWCI_TEST_SCRIPT"
EXIT_CODE=$?
set -e

# Let's make sure the results are always stored in current working directory
mv -f ${CI_PROJECT_DIR}/results ./ 2>/dev/null || true

[ ${EXIT_CODE} -ne 0 ] || rm -rf results/trace/"$PIGLIT_REPLAY_DEVICE_NAME"

# Make sure that capture-devcoredump is done before we start trying to tar up
# artifacts -- if it's writing while tar is reading, tar will throw an error and
# kill the job.
cleanup

# upload artifacts
if [ -n "$S3_RESULTS_UPLOAD" ]; then
  tar --zstd -cf results.tar.zst results/;
  ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" results.tar.zst https://"$S3_RESULTS_UPLOAD"/results.tar.zst;
fi

# We still need to echo the hwci: gstreamer message, as some scripts rely on it, such
# as the python ones inside the bare-metal folder
[ ${EXIT_CODE} -eq 0 ] && RESULT=pass || RESULT=fail

set +x

# Print the final result; both bare-metal and LAVA look for this string to get
# the result of our run, so try really hard to get it out rather than losing
# the run. The device gets shut down right at this point, and a630 seems to
# enjoy corrupting the last line of serial output before shutdown.
for _ in $(seq 0 3); do echo "hwci: gstreamer: $RESULT"; sleep 1; echo; done

exit $EXIT_CODE
