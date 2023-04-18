#!/bin/bash

BEAK=$1
BEAKMEDIA=${BEAK}-media
DIR=$2

good=$($BEAKMEDIA import 2>&1)
bad=$($BEAK import 2>&1)

if [ "$good" != "beak: (commandline) Command expects origin as first argument." ]
then
    echo "Cmdline test 1 failed! $good"
    exit 1
fi
if [ "$bad" != "This is beak, please use beak-media for the command \"import\"" ]
then
    echo "Cmdline test 2 failed! $bad"
    exit 1
fi

good=$($BEAK push 2>&1)
bad=$($BEAKMEDIA push 2>&1)

if [ "$good" != "beak: (commandline) Command expects rule as first argument." ]
then
    echo "Cmdline test 1 failed! $good"
    exit 1
fi
if [ "$bad" != "This is beak-media, please use plain beak for the command \"push\"" ]
then
    echo "Cmdline test 2 failed! $bad"
    exit 1
fi
