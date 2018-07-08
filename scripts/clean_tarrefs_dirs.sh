#!/bin/bash

MOUNTS=$(cat /proc/mounts  | grep -o -P '/tmp/beak_[[:alnum:]]+/[^ ]+')
if [ -n "$MOUNTS" ]; then
    for m in $MOUNTS; do
        fusermount -u $m
        echo Unmounting $m
    done
fi

BEAKS=$(echo /tmp/beak_*)
if [ ! "$BEAKS" = "/tmp/beak_*" ]; then
    for d in $BEAKS; do
        find $d -type d ! -perm /u+w -exec chmod u+w \{\} \;
        rm -rf $d
        echo Removing $d
    done
fi
