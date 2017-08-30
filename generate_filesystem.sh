#!/bin/bash

# Seed the random number generator to the same seed always.
RANDOM=1

maxdepth=5

rndname()
{
    len=$((($RANDOM % 32)+1));
    cat /dev/urandom | tr -dc "[:alpha:]" | head -c $len
}

createdir()
{
    currdir="$1"
}

createfs()
{
    currdir="$1"
    currdepth="$2"
    mkdir -p "$currdir"
    if [ "$currdepth" -lt "$maxdepth" ]
    then
        numsubdirs=$((($RANDOM % 3)));
        numfiles=$((($RANDOM % 32)));
        
        i=0
        while [ $i -lt $numfiles ]
        do
            name=$(rndname)
            len=$RANDOM;
            dd if=/dev/urandom of="$currdir/$name" bs=$blocksize count=$len > /dev/null 2>&1
            ((i++))
        done
        
        i=0
        while [ $i -lt $numsubdirs ]
        do
            newdir=$(rndname)
            createfs "$currdir/$newdir" $(($currdepth + 1))
            ((i++))
        done
    fi
}


if [ ! -d "$1" ]
then
    echo Usage: generate_filesystem.sh [dir] [depth] [blocksize]
    exit 1
fi

root="$1"
depth="$2"
blocksize="$3"

createfs $1 1
