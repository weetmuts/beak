#!/usr/bin/env bash
IFS=$'\n\t'
set -eu
#
#    Copyright (C) 2016-2023 Fredrik Öhrström
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

if [ "$BASH_VERSION" = "" ]
then
    echo "You have to run this script with bash!"
    exit 1
fi

if [[ $BASH_VERSION =~ ^3\. ]]
then
    echo "You have to run an up to date bash! This is version $BASH_VERSION but you should use 5 or later."
    exit 1
fi

function findGnuProgram()
{
    PROG=$(whereis -b $1 | cut -f 2 -d ' ' )
    local CHECK=$(($PROG --version 2>&1 || true) | grep -o GNU | uniq)
    if [ "$CHECK" != "GNU" ]
    then
        PROG=$(whereis -b $2 | cut -f 2 -d ' ')
        local CHECK=$(($PROG --version 2>/dev/null || true) | grep -o GNU | uniq)
        if [ "$CHECK" != "GNU" ]
        then
            >&2 echo "This script requires either $1 or $2 to be the GNU version!"
            exit 1
        fi
    fi
    echo $PROG
}

SED=$(findGnuProgram sed gsed)
TAR=$(findGnuProgram tar gtar)
AWK=$(findGnuProgram awk gawk)
DATE=$(findGnuProgram date gdate)
TR=$(findGnuProgram tr gtr)
ZCAT=$(findGnuProgram zcat gzcat)

function finish {
    if [ "$debug" == "" ]
    then
        rm -rf $dir
    else
        echo Not removing $dir
    fi
}
trap finish EXIT

if ! [ -x "$(command -v sha256sum)" ]; then
    sha256sum() { shasum -a 256 "$@" ; }
fi

# Replacement for realpath
realpath() {
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

function Help() {
    echo Usage: beak-restore {-d} {-c} [x\|t]{a}{v} [DirWithTars] [TargetDir]
    echo
    echo Example:
    echo beak-restore x /Mirror/Storage .
    echo beak-restore xv /Mirror/Storage/Articles /home/storage
    echo beak-restore xa /Mirror/Storage/Articles /home/storage
    echo beak-restore tav /Mirror/Storage/Work
    echo
    echo Add -d to debug.
    exit
}

function pushDir() {
    if [ "$extract" = "true" ]; then
        mkdir -p "$target_dir"
        pushd "$target_dir" > /dev/null
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

if [ -z ${1+x} ] || [ -z ${2+x} ]; then
    Help
fi

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

if [ -z ${2+x} ]
then
    root=""
else
    root="$2"
fi

if [ -z ${3+x} ]
then
    target=""
else
    target="$3"
fi

root="$(realpath "$root")"
target="$(realpath "$target")"

if [ "$cmd" != "t" ] && [ "$target" = "" ]
then
    Help
fi

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

if [ "$cmd" = "" ] || [ "$root" = "" ]
then
    Help
fi

numgzs=$(ls "$root"/beak_z_*.gz | sort -r | wc | $TR -s ' ' | cut -f 2 -d ' ')

if [ "$numgzs" = "0" ]; then
    echo No beakfs found in "$root"
    exit
fi

if [ "$numgzs" = "1" ]; then
    if [ "$gen" != "" ] && [ "$gen" != "@0" ] ; then
        echo Only generation @0 exists!
        exit
    fi
    generation=$(echo "$root"/beak_z_*.gz)
else
    ls "$root"/beak_z_*.gz | sort -r > "$dir/generations"
    if [ "$gen" = "" ]; then
        echo More than one beakfs generation found!
        echo Select a generation using -g
        n=0
        while IFS='' read i; do
            msg=$(gunzip -c "$i" 2>/dev/null | head -2 | grep \#message | $SED 's/#message //')
            secs=$(echo "$i" | $SED "s/.*beak_z_\([0-9]\+\).*/\1/")
            dat=$($DATE--date "@$secs")
            echo -e "@$n\t$dat\t$msg"
            n=$((n+1))
        done <"$dir/generations"
        exit
    else
        gen="${gen##@}"
        gen=$((gen+1))
        generation=$($SED -n ${gen}p "$dir/generations")
    fi
fi

# Check the internal checksum of the index file.
# (2373686132353620 is hex for "#end ")
CALC_CHECK=$($ZCAT < "$generation" 2>/dev/null | xxd -p | $TR -d '\n' | \
                    $SED 's/23656e6420.*//' | xxd -r -p | sha256sum | cut -f 1 -d ' ')
READ_CHECK=$($ZCAT < "$generation" 2>/dev/null | $TR -d '\0' | grep "#end" | cut -f 2 -d ' ')

if [ ! "$CALC_CHECK" = "$READ_CHECK" ]
then
    echo The checksum of the index file $generation could not be verified!
    echo "Stored in index: $READ_CHECK"
    echo "Calculated:      $CALC_CHECK"
    exit 1
fi
# Extract the list of tar files from the index file.
gunzip -c "$generation" 2>/dev/null \
    | grep -a -A1000000 -m1 \#tars \
    | grep -a -B1000000 -m1 \#parts \
    | grep -a -v \#tars \
    | grep -a -v \#parts  > "$dir/aa"

# Extract the paths to the tarfiles
cat "$dir/aa" | $SED 's/.*\x00//'  > "$dir/tarfiles"

# Extract the backup location for each tarfile
cat "$dir/aa" | $SED 's/\x00\([^\x00]*\)\x00.*/\1/' > "$dir/backup_locations"

# Extract the potential basis_tarfile for each tarfile
cat "$dir/aa" | $SED 's/\x00\[^\x00]*\x00\([^\x00]*\).*/\1/' > "$dir/basis_tarfiles"

# Extract the potential delta_tarfile for each tarfile
cat "$dir/aa" | $SED 's/\x00\[^\x00]*\x00\[^\x00]*\x00\([^\x00]*\).*/\1/' > "$dir/delta_tarfiles"

# Generate slashes
cat "$dir/backup_locations" | $TR -c -d '/\n' | $TR / a > "$dir/slashes"

# Sort them on the number of slashes, ie handle the
# deepest directories first, finish with the root
# (replaced / with a to make sort work)
paste "$dir/slashes" "$dir/backup_locations" "$dir/tarfiles" "$dir/basis_tarfiles" "$dir/delta_tarfiles" | LC_COLLATE=en_US.UTF8 sort > "$dir/sorted_tars"

if [ "$debug" == "true" ]
then
    echo "$dir/aa"
    echo "$dir/slashes"
    echo "$dir/backup_locations"
    echo "$dir/tarfiles"
    echo "$dir/basis_tarfiles"
    echo "$dir/delta_tarfiles"
    echo "$dir/sorted_tars"
fi

# Iterate over the tar files and extract them
# in the corresponding directory. Read store a line at a time from $dir/ee into $tar_file
while read -r depth backup_location tar_file basis delta
do
    if [ "$debug" == "true" ]
    then
        echo depth=$depth
        echo backup_location=$backup_location
        echo tarfile=$tar_file
        echo basis=$basis
        echo delta=$delta
    fi

    last_file=""
    # Test for split tar i02_123123123.tar ... i02_123123122.tar
    if [ "$(echo "$tar_file" | grep -o " \\.\\.\\. ")" = " ... " ]
    then
        tmp="$tar_file"
        tar_file="$(echo "$tar_file" | $SED 's/\(.*\)\ \.\.\.\ .*/\1/')"
        last_file="$(echo "$tmp" | $SED 's/.*\ \.\.\.\ \(.*\)/\1/')"
        if [ "$debug" == "true" ]
        then
            echo "Split file"
            echo tar_file=$tar_file
            echo last_file=$last_file
        fi
    fi

    # Extract directory in which the tar file resides.
    target_dir="$target/$backup_location"
    # Rename the top directory . into the empty string.
    target_dir_prefix="${backup_location#.}"

    file=$(echo "$root/$tar_file"*)
    if [ ! -f "$file" ]; then
        echo Error file "$file" does not exist!
        exit
    fi

    # Test if gz index file containing a tar.
    if [[ $file == *.gz ]] && [[ $file != *.tar.gz ]]
    then
        # Extract the directory and hard links and rdiff patches.
        pushDir
        POS=$($ZCAT < "$file" | grep -ab "#end" | cut -f 1 -d ':')
        $ZCAT < "$file" | dd skip=$((POS + 72)) ibs=1 2> /dev/null > ${dir}/beak_restore.tar
        # $ZCAT < "$file" 2>/dev/null | xxd -p  | $TR -d '\n' | $SED 's/.*23656e6420.\{128\}0a00//' | xxd -r -p > /tmp/beak_restoree.tar

        if [ -s ${dir}/beak_restore.tar ]
        then
            CMD="$TAR ${cmd}f ${dir}/beak_restore.tar --preserve-permissions"
            if [ "$verbose" == "true" ]; then echo CMD="$CMD"; fi
            eval $CMD > $dir/tmplist
            if [ "$?" != "0" ]; then
                echo Failed: "$CMD"
            fi
            if [ "$extract" == "true" ]; then
                # GNU Tar simply prints the filename when extracting verbosely.
                # Simply prefix the tar dir.
                $AWK -v prefix="$target_dir_prefix" '{print prefix $0}' $dir/tmplist
            else
                # GNU Tar prints permissions, date etc when viewing verbosely.
                $AWK '{p=match($0," [0-9][0-9]:[0-9][0-9] "); print substr($0,0,p+6)"'" $target_dir_prefix"'"substr($0,p+7)}' $dir/tmplist
            fi
            rm $dir/tmplist
            popDir
        fi
    elif [ "$last_file" = "" ]
    then
        if [ "$debug" == "true" ]
        then
            echo "Single file"
        fi
        # Single part file.
        CMD="$TAR ${cmd}f \"$file\" --warning=no-alone-zero-block --preserve-permissions"
        pushDir
        if [ "$verbose" = "true" ]; then echo CMD="$CMD"; fi
        eval $CMD > $dir/tmplist
        if [ "$?" != "0" ]; then
            echo Failed: "$CMD"
        fi
        if [ "$extract" == "true" ]; then
            # GNU Tar simply prints the filename when extracting verbosely.
            # Simply prefix the tar dir.
            $AWK -v prefix="$target_dir_prefix" '{print prefix $0}' $dir/tmplist
        else
            # GNU Tar prints permissions, date etc when viewing verbosely.
            $AWK '{p=match($0," [0-9][0-9]:[0-9][0-9] "); print substr($0,0,p+6)"'" $target_dir_prefix"'"substr($0,p+7)}' $dir/tmplist
        fi
        rm $dir/tmplist
        popDir
    else
        if [ "$debug" == "true" ]
        then
            echo "Multi part file"
        fi
        # Multi part file!
        prefix=$(echo "$tar_file" | $SED 's/\(.*_\)[0-9a-f]\+-.*/\1/')
        suffix=".tar"
        first=$(echo "$tar_file" | $SED 's/.*_\([0-9a-f]\+\)-.*/\1/')
        first=$((0x$first))
        numx=$(echo "$tar_file" | $SED 's/.*-\([0-9a-f]\+\)_.*/\1/')
        num=$((0x$numx))
        disksize=$(echo "$tar_file" | $SED 's/.*_\([0-9]\+\)\.tar/\1/')
        size=$(echo "$tar_file" | $SED 's/.*_\([0-9]\+\)_[0-9]\+\.tar/\1/')
        partnrwidth=$(echo -n $numx | wc -c)

        if [ "$(echo "$last_file" | grep -o "$prefix")" = "$prefix" ]
        then
            last=$(echo "$last_file" | $SED 's/.*_\([0-9a-f]\+\)-.*/\1/')
            last=$((0x$last))
            nummx=$(echo "$last_file" | $SED 's/.*-\([0-9a-f]\+\)_.*/\1/')
            numm=$((0x$nummx))
            disklastsize=$(echo "$last_file" | $SED "s/.*_\([0-9]\+\)\.tar/\1/")
            lastsize=$(echo "$last_file" | $SED 's/.*_\([0-9]\+\)_[0-9]\+\.tar/\1/')

            newvolumescript="${dir}/beak_tarvolchange.sh"
            cat > ${newvolumescript} <<EOF
#!/bin/bash

part=\$((TAR_VOLUME))

if [ "\$part" = "$((1+$num))" ]
then
    exit 1
fi

format="$(printf "%%s%%0%dx-%s_%%s_%%s%%s" ${partnrwidth} ${numx})"

if [ "\$part" = "$num" ]
then
    partsize="$lastsize"
    disksize="$disklastsize"
else
    partsize="$size"
    disksize="$disksize"
fi

foo=\$(printf "\${format}" "${root}/${prefix}" "\${part}" "\${partsize}" "\${disksize}" "${suffix}")

# Cut the file to partsize (no padding), otherwise tar will be unhappy.
dd if="\${foo}" of="${dir}/beak_part" bs=512 count=\$((partsize / 512)) > /dev/null 2>&1
echo ${dir}/beak_part >&\$TAR_FD
EOF

            chmod a+x ${newvolumescript}
            pushDir
            # Gnu tar prints unnecessary warnings when extracting multivol files
            # with long path names. Also there is always an error when the last
            # multivol part has been extract. Hide this with pipe to null.

            # Cut the file to partsize (no padding), otherwise tar will be unhappy.
            dd if="${root}/${tar_file}" of="${dir}/beak_part" bs=512 count=$((size / 512)) > /dev/null 2>&1
            # Invoke the tar command and or with true, to hide the silly failed return value from tar.
            ($TAR ${cmd}Mf "${dir}/beak_part" -F ${newvolumescript} > /dev/null 2>&1) || true
            popDir
        else
            echo Broken multipart listing in index file, prefix not found.
        fi
     fi

done <"$dir/sorted_tars"

# Extract the final tar contents from the index file.
target_dir="$target/$backup_location"
target_dir_prefix="/"

POS=$($ZCAT < "$generation" | grep -ab "#end" | cut -f 1 -d ':')
$ZCAT < "$generation" | dd skip=$((POS + 72)) ibs=1 2> /dev/null > ${dir}/beak_restore.tar
if [ -s ${dir}/beak_restore.tar ]
then
    CMD="$TAR ${cmd}f ${dir}/beak_restore.tar --preserve-permissions"
    if [ "$verbose" == "true" ]; then echo CMD="$CMD"; fi
    pushDir
    eval $CMD > $dir/tmplist
    if [ "$?" != "0" ]; then
        echo Failed: "$CMD"
    fi
    if [ "$extract" == "true" ]; then
        # GNU Tar simply prints the filename when extracting verbosely.
        # Simply prefix the tar dir.
        $AWK -v prefix="$target_dir_prefix" '{print prefix $0}' $dir/tmplist
    else
        # GNU Tar prints permissions, date etc when viewing verbosely.
        $AWK '{p=match($0," [0-9][0-9]:[0-9][0-9] "); print substr($0,0,p+6)"'" $target_dir_prefix"'"substr($0,p+7)}' $dir/tmplist
    fi
    rm $dir/tmplist
    popDir
fi
