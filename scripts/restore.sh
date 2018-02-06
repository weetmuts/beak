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

dir=$(mktemp -d /tmp/beak_restoreXXXXXXXX)

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
    echo Usage: beak-restore {-d} {-c} [x\|t]{a}{v} [DirWithTars] {PathToExtract}
    echo
    echo Example:
    echo beak-restore x /Mirror/Storage
    echo beak-restore xv /Mirror/Storage/Articles
    echo beak-restore xa /Mirror/Storage/Articles mag1.pdf
    echo beak-restore tav /Mirror/Storage/Work
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
gen=''

while [[ $1 =~ -.* ]]
do
    case $1 in
        -d) debug='true'
            shift ;;
    esac
    case $1 in
        -c) check='true'
            shift ;;
    esac
    case $1 in
        -g) gen="$2"
            shift
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
        ext='{.xz,.gz,.bz2,}'
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

numgzs=$(ls "$root"/z01*.gz | sort -r | wc | tr -s ' ' | cut -f 2 -d ' ')

if [ "$numgzs" = "0" ]; then
    echo No beakfs found in "$root"
    exit
fi

if [ "$numgzs" = "1" ]; then
    if [ "$gen" != "" ] && [ "$gen" != "@0" ] ; then
        echo Only generation @0 exists!
        exit
    fi
    generation=$(echo "$root"/z01*.gz)
else
    ls "$root"/z01*.gz | sort -r > "$dir/generations"
    if [ "$gen" = "" ]; then
        echo More than one beakfs generation found!
        echo Select a generation using -g
        n=0
        while IFS='' read i; do
            msg=$(gunzip -c "$i" | head -2 | grep \#message | sed 's/#message //')
            secs=$(echo "$i" | sed "s/.*z01_\([0-9]\+\).*/\1/")
            dat=$(date --date "@$secs")
            echo -e "@$n\t$dat\t$msg"
            n=$((n+1))
        done <"$dir/generations"
        exit
    else
        gen="${gen##@}"
        gen=$((gen+1))
        generation=$(sed -n ${gen}p "$dir/generations")
    fi
fi

# Extract the list of tar files from the index file.
gunzip -c "$generation" | tr -d '\0' | grep -A1000000 -m1 \#tars | grep -v z01 | grep -v \#tars | sed 's/^\///'  > "$dir/aa"
cat "$dir/aa" | tr -c -d '/\n' | tr / a > "$dir/bb"
# Sort them on the number of slashes, ie handle the
# deepest directories first, finish with the root
# (replaced / with a to make sort work)
paste "$dir/bb" "$dir/aa" | LC_COLLATE=en_US.UTF8 sort | cut -f 2- > "$dir/cc"

# Iterate over the tar files and extract them
# in the corresponding directory. Read store a line at a time from $dir/cc into $tar_file
while IFS='' read tar_file; do
    # Extract directory in which the tar file resides.
    tar_dir="$(dirname "$tar_file")"
    # Rename the top directory . into the empty string.
    tar_dir_prefix="${tar_dir#.}"
    if [ "$tar_dir_prefix" != "" ]; then
        # Make sure prefix ends with a slash.
        tar_dir_prefix="$tar_dir_prefix"/
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

    file=$(echo "$root/$tar_file"*)
    if [ ! -f "$file" ]; then
        echo Error file "$file" does not exist!
        exit
    fi

    if [ "$verbose" = "true" ]; then
        CMD="tar ${cmd}f \"$file\" \"$ex\" $ignore_errs"
        if [ "$try_tar" == "true" ]; then
            pushDir
            if [ "$debug" == "true" ]; then echo "$CMD"; fi
            eval $CMD > $dir/tmplist
            popDir
            if [ "$?" == "0" ]; then
                echo Beak: tar ${cmd}f \"$file\" \"$ex\"
            fi
            if [ "$extract" == "true" ]; then
                # GNU Tar simply prints the filename when extracting verbosely.
                # Simply prefix the tar dir.
                awk -v prefix="$tar_dir_prefix" '{print prefix $0}' $dir/tmplist
            else
                # GNU Tar prints permissions, date etc when viewing verbosely.
                awk '{p=match($0," [0-9][0-9]:[0-9][0-9] "); print substr($0,0,p+6)"'" $tar_dir_prefix"'"substr($0,p+7)}' $dir/tmplist
            fi
            rm $dir/tmplist
        else
            if [ "$debug" == "true" ]; then echo Skipping "$file"; fi
        fi

    else
        if [ "$try_tar" == "true" ]; then
            CMD="tar ${cmd}f \"$file\" \"$ex\" $ignore_errs"
            pushDir
            if [ "$debug" == "true" ]; then echo "$CMD"; fi
            eval $CMD
            popDir
            if [ "$?" != "0" ] && [ "$ignore_errs" == "" ]; then
                echo Failed when executing: tar ${cmd}f \"$file\"
                exit
            fi
        else
            if [ "$debug" == "true" ]; then echo Skipping "$root/$tar_file"; fi
        fi
    fi

done <"$dir/cc"
