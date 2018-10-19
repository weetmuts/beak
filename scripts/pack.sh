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

dir=$(mktemp -d /tmp/beak_packXXXXXXXX)

function finish {
    if [ "$debug" == "" ]
    then
        # Remove the currently compressed/copied file.
        # Since compression/copying was interrupted, it might
        # be incompleted.
        if [ "$done" != "true" ]; then
            rm -f "$to" "$to$ext"
            rm -rf $dir
        fi
    else
        echo Not removing $dir
    fi
}
trap finish EXIT

function Help() {
    echo
    echo Usage: beak-pack [xz\|gzip\|bzip2] [DirWithTars] [DirWithPackedTars]
    echo
    echo Example:
    echo beak-pack xz /Mirror/Storage /Mirror/CompressedStorage
    echo beak-pack gzip /Mirror/Storage/Articles /home/CompressedBackup
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

if [ "$cmd" = "" ] || [ "$2" = "" ] || [ "$3" = "" ]; then
    Help
fi

srcdir=$(realpath $2)
destdir=$(realpath $3)

# Find the tar files
(find $srcdir -regextype grep -regex ".*/[smlzy]0[12]_.*\.\(tar\|gz\)"  | sort) > "$dir/aa"

while read from; do
    to="$destdir${from##$srcdir}"
    parent="${to%/*}"
    mkdir -p "$parent"
    extension="${to##*.}"
    if [ "$extension" = "tar" ]; then
        if [ ! -f "$to$ext" ]; then
            filename="${from##$srcdir/}"
            (cd "$srcdir" ; $cmd --verbose -c "$filename" > "$to$ext" ;
             touch -r "$filename" "$to$ext" ;
             chmod --reference="$filename" "$to$ext")
        fi
    else
        if [ ! -f "$to" ]; then
            cp -a "$from" "$to"
        fi
    fi
done <"$dir/aa"

done=true
