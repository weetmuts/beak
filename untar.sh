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

dir=$(mktemp -d /tmp/tarredfs_untarXXXXXXXX)

function finish {
    if [ "$debug" == "" ]
    then
        rm -rf $dir
    else
        echo Not removing $dir
    fi
}
trap finish EXIT

function Help() {
    echo
    echo Usage: tarredfs-untar {-d} {-c} [x\|t]{a}{v} [DirWithTars] {PathToExtract}
    echo
    echo Example:
    echo tarredfs-untar x /Mirror/Storage
    echo tarredfs-untar xv /Mirror/Storage/Articles
    echo tarredfs-untar xa /Mirror/Storage/Articles mag1.pdf
    echo tarredfs-untar tav /Mirror/Storage/Work
    echo
    echo Add -d to debug.
    exit
}

function pushDir() {
    if [ "$extract" = "true" ]; then
        mkdir -p "$tar_dir"
        pushd "$tar_dir" > /dev/null
        if [ "$debug" == "true" ]; then echo pushd $(pwd); fi        
    fi    
}

function popDir() {
    if [ "$extract" = "true" ]; then
        popd > /dev/null
        if [ "$debug" == "true" ]; then echo pop; fi        
    fi
}

debug=''
check=''
cmd=''
verbose=''
extract=''
ext=''

while [[ $1 =~ -.* ]]
do
    case $1 in
        -d) debug='true'
            shift ;;
    esac
    case $2 in
        -c) check='true'
            shift ;;
    esac
done

case $1 in
    [xt]) ;;
    [xt]v) ;;
    [xt]a) ;;
    [xt]va) ;;
    [xt]av) ;;
    *) Help
esac

case $1 in
    x*)
        cmd='xp'
        extract='true'
        ;;
    t*)
        cmd='t'
        extract='false'
        ;;
esac

case $1 in
    *a*)
        cmd=${cmd}a
        ext='(\.xz|\.gz|\.bz2|)$'
        ;;
esac

case $1 in
    *v*)
        cmd=${cmd}v
        verbose='true'
        ;;
esac

if [ "$cmd" = "" ] || [ "$2" = "" ]; then
    Help
fi

extract_this=""
if [ "$3" != "" ]; then
    extract_this="$3"
fi

root="$(realpath $2)"
# Find the tar files
(cd "$root" && find . -type f -regextype awk -regex ".*/ta[rmlz][0-9a-z][0-9a-z][0-9a-z][0-9a-z][0-9a-z][0-9a-z][0-9a-z][0-9a-z]\.tar${ext}" | sed 's/^\.\///' | sort) > "$dir/aa"
cat "$dir/aa" | tr -c -d '/\n' | tr / a > "$dir/bb"
# Sort them on the number of slashes, ie handle the
# deepest directories first, finish with the root
# (replaced / with a to make sort work)
paste "$dir/bb" "$dir/aa" | LC_COLLATE=en_US.UTF8 sort | cut -f 2- > "$dir/cc"

# Iterate over the tar files and extract them
# in the corresponding directory
while IFS='' read tar_file; do
    # Extract directory in which the tar file resides.
    tar_dir="$(dirname "$tar_file")"
    # Rename the top directory . into the empty string.
    tar_dir_prefix="${tar_dir#.}"
    if [ "$tar_dir_prefix" != "" ]; then
        # Make sure prefix ends with a slash.
        tar_dir_prefix="$tar_dir_prefix/"
    fi
    try_tar='true'
    if [ "$extract_this" != "" ]; then
        if [[ "$extract_this" =~ ${tar_dir_prefix}.* ]]
        then
            # If something should be extracted, then remove the tar_dir_prefix from its path.
            ex="${extract_this##$tar_dir_prefix}"
            try_tar='true'
            # Also ignore not found errors from tar
            ignore_errs="2> /dev/null"         
        else
            try_tar='false'
            ex=''
        fi
    fi

    if [ "$verbose" = "true" ]; then

        CMD="tar ${cmd}f \"$root/$tar_file\" --exclude='tarredfs-contents' \"$ex\" $ignore_errs"
        if [ "$try_tar" == "true" ]; then            
            pushDir
            if [ "$debug" == "true" ]; then echo "$CMD"; fi
            eval $CMD > $dir/tmplist
            popDir
            if [ "$?" == "0" ]; then
                echo Tarredfs: tar ${cmd}f "$tar_file" --exclude='tarredfs-contents' "$ex"
            fi
            # Insert the tar_dir path as a prefix of the tar contents.
            awk '{p=match($0," [0-9][0-9]:[0-9][0-9] "); print substr($0,0,p+6)"'"$tar_dir"'/"substr($0,p+7)}' $dir/tmplist
            rm $dir/tmplist
        else
            if [ "$debug" == "true" ]; then echo Skipping "$root/$tar_file"; fi
        fi

    else
        if [ "$try_tar" == "true" ]; then
            CMD="tar ${cmd}f \"$root/$tar_file\" --exclude='tarredfs-contents' \"$ex\" $ignore_errs"
            pushDir
            if [ "$debug" == "true" ]; then echo "$CMD"; fi
            eval $CMD
            popDir
            if [ "$?" != "0" ] && [ "$ignore_errs" == "" ]; then
                echo Failed when executing: tar ${cmd}f "$root/$tar_file"
                exit
            fi
        else
            if [ "$debug" == "true" ]; then echo Skipping "$root/$tar_file"; fi
        fi
    fi
    
done <"$dir/cc"



