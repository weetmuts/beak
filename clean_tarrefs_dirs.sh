#!/bin/bash

MOUNTS=$(cat /proc/mounts  | grep -o -P '/tmp/beak_[[:alnum:]]+/[^ ]+')
if [ -n "$MOUNTS" ]; then
    echo Unmounting $MOUNTS
    for m in $MOUNTS; do fusermount -u $m ; done
fi

rm -rf /tmp/beak_*
