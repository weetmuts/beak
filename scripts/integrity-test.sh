#!/usr/bin/env bash
#
#    Copyright (C) 2017-2023 Fredrik Öhrström
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

dir=$(mktemp -d /tmp/beak_integrityXXXXXXXX)

function finish {
    if [ "$debugdirset" == "" ] && [ "$debug" == "" ]
    then
        rm -rf $dir
    else
        if [ "$debugdirset" == "" ]; then
            echo Not removing $dir
        fi
    fi
}
trap finish EXIT

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

LS=$(findGnuProgram ls gls)
FIND=$(findGnuProgram find gfind)

function Help() {
    echo Usage: beak-integrity-test {-d} {-onlypart2} {-f [find-expression]} [root] [mount] {time}
    echo
    echo Check that the contents listed in the tar files inside mount
    echo corresponds exactly to the contents listed in root.
    echo This is done without actually extracting them.
    echo
    echo If a time is given \(10s, 1m 1h\), then it will continuously
    echo pick a random check file, extract it from the mount and compare that
    echo the check file is identical to the original. The check file is then deleted.
    echo
    echo -d will run in debug mode and not erase the temp files
    echo -part2 will skip the first part checking the listings.
    echo -f will filter the root results before comparison with the mount tars.
    exit
}

debug=''
debugdirset=''
onlypart2=''
filter=''

while [[ $1 =~ -.* ]]
do
    case $1 in
        -d) debug='true'
            shift ;;
        -dd) debugdirset='true'
             dir="$2"
             shift
             shift ;;
        -f) filter="$2"
            shift
            shift ;;
        -onlypart2) onlypart2='true'
            shift ;;
        *) echo Unknown option $1
           exit ;;
    esac
done

root="$1"
mount="$2"
time="$3"

if [ "$root" == "" ] || [ "$mount" == "" ]; then
    Help
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ "$DIR" = "/usr/local/bin" ]; then
    PERL_PREFIX=/usr/local/lib/beak/
    UNTAR=/usr/local/bin/beak-restore
else
    PERL_PREFIX="$DIR"/
    UNTAR="$DIR/restore.sh"
fi


if [ "$onlypart2" = "" ]
then
    # Find all files under root (using a potential filter)
    (cd $root && eval "$FIND . $filter -printf '%M\0%u/%g\0%s\0%TY-%Tm-%Td\0%TH:%TM\0%p\0%l\n'") \
        > $dir/test_root_raw.txt

    cat $dir/test_root_raw.txt | perl ${PERL_PREFIX}format_find.pl $root > $dir/test_root.txt

    # Find all files listed in the tar files below mount.
    ${UNTAR} tv $mount > $dir/test_tar_raw.txt
    cat $dir/test_tar_raw.txt | perl ${PERL_PREFIX}format_tar.pl > $dir/test_tar.txt

    sort $dir/test_root.txt > $dir/test_root_sorted.txt
    sort $dir/test_tar.txt > $dir/test_tar_sorted.txt

    diff $dir/test_root_sorted.txt $dir/test_tar_sorted.txt > $dir/difff
    rc=$(cat $dir/difff)
    if [ "$rc" = "" ]; then
        echo OK
    else
        cat $dir/difff
        echo "============== $dir/test_root_sorted.txt"
        cat $dir/test_root_sorted.txt
        echo "============== $dir/test_tar_sorted.txt"
        cat $dir/test_tar_sorted.txt

        debug='true'
        exit 1
    fi
fi

if [ "$onlypart2" != "" ] && [ "$time" = "" ]
then
    echo You must set a time when using -onlypart2
    exit
fi

if [ "$time" != "" ]
then
    case $time in
        *s) ;;
        *m) $time=$(($time*60)) ;;
        *h) $time=$(($time*3600)) ;;
    esac

    start_time=$(date +%s)
    curr_time=$(($start_time + $time))
    time_diff=0

    mkdir $dir/Check
    cd $dir/Check

    # Now only extract standard files.
    (cd $root && eval "$FIND . -type f $filter -printf '%p\n'") > $dir/files.txt

    while (( $time_diff < $time ))
    do
        curr_time=$(date +%s)
        time_diff=$(($curr_time - $start_time))
        file=$(shuf $dir/files.txt | head -n 1)
        file=${file#./}

        ${UNTAR} x "$mount" "$file"
        diff "$root/$file" "$file"
        if [ "$?" = "0" ]; then
            echo echo "OK ($time_diff):" "$file"
        else
            echo Diff failed:
            echo Original:  "$root/$file"
            echo Extracted: "dir/Check/$file"
            exit
        fi
        rm -f "$dir/Check/$file"
        curr_time=$(date +%s)
    done
fi
