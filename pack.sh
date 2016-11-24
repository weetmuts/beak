#!/bin/bash
#  
#    Copyright (C) 2016 Fredrik Öhrström
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

function Help() {
    echo
    echo Usage: tarredfs-pack [xz\|gzip\|bzip2] [DirWithTars]
    echo
    echo Example:
    echo tarredfs-pack xz /Mirror/Storage
    echo tarredfs-pack gzip /Mirror/Storage/Articles
    exit
}

cmd=''

case $1 in
    xz)
        cmd='xz'
        ext='.xz'
        ;;
    gzip)
        cmd='gzip'
        ext='.gz'
        ;;
    bzip2)
        cmd='bzip2'
        ext='.bz2'
        ;;
esac

if [ "$cmd" = "" ] || [ "$2" = "" ]; then
    Help
fi

root=$(realpath $2)
# Find the tar files
(cd $root && find . -name "ta[rz]*tar" | sed 's/^\.\///' | sort) > /tmp/aa
cat /tmp/aa | tr -c -d '/\n' | tr / a > /tmp/bb
# Sort them on the number of slashes, ie handle the
# deepest directories first, finish with the root
# (replaced / with a to make sort work)
paste /tmp/bb /tmp/aa | sort | cut -f 2- > /tmp/cc
# Iterate over the tar files and extract them
# in the corresponding directory
while read p; do
    parent="$(dirname $p)"
    curr="$(basename $p)"
    mkdir -p "$parent"
    pushd "$parent" > /dev/null
    $cmd --verbose -c $root/$p > $curr$ext
    popd > /dev/null
done </tmp/cc
