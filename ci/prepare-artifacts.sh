#!/usr/bin/env bash
# shellcheck disable=SC2038 # TODO: rewrite the find
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

# Install built gstreamer
meson install -C build

# Delete unused bin and includes from artifacts to save space
rm -rf install/include install/libexec

find install -name \*.a -exec rm {} \;

STRIP="strip"
if [ -z "$ARTIFACTS_DEBUG_SYMBOLS" ]; then
    find install -name \*.so -exec $STRIP --strip-debug {} \;
fi

cp -Rp ci/common install/
cp -Rp ci/fluster install/
cp -Rp ci/*.txt install/
cp -Rp ci/setup-test-env.sh install/
find . -path \*/ci/\*.txt \
    -o -path \*/ci/\*.toml \
    | xargs -I '{}' cp -p '{}' install/

# Tar up the install dir so that symlinks and hardlinks aren't each
# packed separately in the zip file.
mkdir -p artifacts/
tar -cf artifacts/install.tar install
cp -Rp ci/common artifacts/ci-common
cp -Rp ci/lava artifacts/

if [ -n "$S3_ARTIFACT_NAME" ]; then
    # Pass needed files to the test stage
    S3_ARTIFACT_NAME="$S3_ARTIFACT_NAME.tar.zst"
    zstd artifacts/install.tar -o ${S3_ARTIFACT_NAME}
    ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" ${S3_ARTIFACT_NAME} https://${PIPELINE_ARTIFACTS_BASE}/${S3_ARTIFACT_NAME}
fi
