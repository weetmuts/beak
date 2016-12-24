#!/bin/bash
#
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

# You can run all tests: test.sh
# You can list all tests: test.sh list
# You can run a single test: test.sh test6
# You can run a single test using gdb: test.sh test6 gdb

tmpdir=$(mktemp -d /tmp/tarredfs_testXXXXXXXX)
dir=""
root=""
mount=""
check=""
log=""
org=""
dest=""
do_test=""

test=$1
gdb=$2

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIS_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

if [ "$1" == "list" ]; then
    grep 'setup test' $THIS_SCRIPT | grep -v grep | cut -f 2- -d ' '
    exit
fi

function setup {
    do_test=""
    if [ -z "$test" ] || [ "$test" == "$1" ]; then
        do_test="yes"
        echo $1: $2
        dir=$tmpdir/$1
        root=$dir/Root
        mount=$dir/Mount
        mountreverse=$dir/MountReverse
        check=$dir/Check
        log=$dir/log.txt
        logreverse=$dir/logreverse.txt
        org=$dir/org.txt
        dest=$dir/dest.txt
        mkdir -p $root
        mkdir -p $mount
        mkdir -p $mountreverse
        mkdir -p $check
    fi
}

function startFS {
    run="$1"
    extra="$2"
    if [ -z "$test" ]; then
        ./build/tarredfs $extra $root $mount > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            ./build/tarredfs -d $extra $root $mount 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args ./build/tarredfs -d $extra $root $mount 
        fi        
    fi        
}

function stopFS {
    (cd $mount; find . -exec ls -ld \{\} \; >> $log)
    fusermount -u $mount
    if [ -n "$test" ]; then
        sleep 2
    fi
    if [ "$1" != "nook" ]; then
        echo OK
    fi
}

function startTwoFS {
    run="$1"
    extra="$2"
    extrareverse="$3"
    if [ -z "$test" ]; then
        ./build/tarredfs $extra $root $mount > $log
        sleep 2
        ./build/tarredfs --reverse $extrareverse $mount $mountreverse > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 4; eval ${run}) &
            ./build/tarredfs $extra $root $mount 2>&1 | tee $log &
            ./build/tarredfs --reverse -d $extrareverse $mount $mountreverse 2>&1 | tee $logreverse &
        else
            (sleep 5; eval ${run}) &
            ./build/tarredfs $extra $root $mount > $log
            sleep 2
            gdb -ex=r --args ./build/tarredfs --reverse -d $extrareverse $mount $mountreverse 
        fi        
    fi        
}

function stopTwoFS {
    (cd $mount; find . -exec ls -ld \{\} \; >> $log)
    (cd $mountreverse; find . -exec ls -ld \{\} \; >> $logreverse)
    fusermount -u $mountreverse
    fusermount -u $mount
    if [ -n "$test" ]; then
        sleep 2
    fi
    if [ "$1" != "nook" ]; then
        echo OK
    fi
}

function untar {
    (cd "$check"; $THIS_DIR/untar.sh x "$mount" "$1")
}

function checkdiff {
    diff -rq $root $check
    if [ $? -ne 0 ]; then
        echo Failed diff for $1! Check in $dir for more information.
        exit
    fi
}

function checkls-ld {
    (cd $root; find . -exec ls -ld \{\} \; > $org)
    (cd $check; find . -exec ls -ld \{\} \; > $dest)
    diff $org $dest
    if [ $? -ne 0 ]; then
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
}

function standardTest {
    untar
    checkdiff
    checkls-ld
    stopFS
}

function fifoTest {
    untar
    checkls-ld
    stopFS
}

function devTest {
    $THIS_DIR/integrity-test.sh -dd "$dir" -f "! -name '*shm*'" /dev "$mount"
    if [ $? -ne 0 ]; then
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
    stopFS nook
}

setup basic01 "Short simple file (fits in 100 char name)"
if [ $do_test ]; then
    echo HEJSAN > $root/hello.txt   
    chmod 400 $root/hello.txt
    startFS standardTest
fi

setup basic02 "Medium path name (fits in 100 char name field and 155 char prefix)"
if [ $do_test ]; then
    tmp=$root/aaaa/bbbb/cccc/dddd/eeee/ffff/gggg/hhhh/iiii/jjjj/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    mkdir -p $tmp
    echo HEJSAN > $tmp'/012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789.txt'
    startFS standardTest
fi

setup basic03 "Long file name (exceeds 100 char name)"
if [ $do_test ]; then
    mkdir -p $root/workspace/InvokeDynamic/opts
    echo HEJSAN > $root'/workspace/InvokeDynamic/opts/sun_nio_cs_UTF_8$Encoder_encodeArrayLoop_Ljava_nio_CharBuffer;Ljava_nio_ByteBuffer;_Ljava_nio_charset_CoderResult;.xml'
    tmp=$root/aaaa/bbbb/cccc/dddd/eeee/ffff/gggg/hhhh/iiii/jjjj/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy/zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
    mkdir -p $tmp
    echo HEJSAN > $tmp'/0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345.txt'
    startFS standardTest
fi

setup basic04 "Exactly 100 char file name (does not fit in 100 char name field due to zero terminating char)"
if [ $do_test ]; then
    tmp=$root/test/test
    mkdir -p $tmp
    echo HEJSAN > $tmp'/0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789'
    startFS standardTest
fi

setup basic05 "Symbolic link"
if [ $do_test ]; then
    echo HEJSAN > $root/test
    ln -s $root/test $root/link
    startFS standardTest
fi

setup basic06 "Symbolic link to long target"
if [ $do_test ]; then
    tmp=$root/01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
    echo HEJSAN > $tmp
    ln -s $tmp $root/link
    startFS standardTest
fi

setup basic07 "FIFO"
if [ $do_test ]; then
    mkfifo $root/fifo1
    mkfifo $root/fifo2
    startFS fifoTest
fi

function filterTest {
    untar
    F=$(cd $check; find . -printf "%p ")
    if [ "$F" != ". ./Beta ./Beta/delta " ]; then
        echo Failed filter test $1! Check in $dir for more information.
        exit        
    fi    
    stopFS
}

setup basic08 "Include exclude"
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    mkdir -p $root/Beta
    echo HEJSAN > $root/Beta/gamma
    echo HEJSAN > $root/Beta/delta
    echo HEJSAN > $root/BetaDelta
    startFS filterTest "-i Beta/ -x mm"
fi

setup basic09 "block and character devices (ie the whole /dev directory)"
if [ $do_test ]; then
    # Testing char and block devices are difficult since they
    # require you to be root. So lets just mount /dev and
    # just list the contents of the tars!
    root=/dev
    startFS devTest "-x shm -p 0"
fi

setup basic10 "check that nothing gets between the directory and its contents"
if [ $do_test ]; then
    # If skrifter.zip ends up after the skrifter directory abd before alfa,
    # then tar's logic for setting the last modified time of the directory will not work.
    mkdir -p $root/TEXTS/skrifter
    echo HEJSAN > $root/TEXTS/skrifter.zip
    echo HEJSAN > $root/TEXTS/skrifter/alfa
    echo HEJSAN > $root/TEXTS/skrifter/beta
    touch -d "2 hours ago" $root/TEXTS/skrifter.zip $root/TEXTS/skrifter/alfa $root/TEXTS/skrifter/beta $root/TEXTS/skrifter 
    mkdir -p $root/libtar/.git
    echo HEJSAN > $root/libtar/.git/config
    echo HEJSAN > $root/libtar/.git/hooks
    echo HEJSAN > $root/libtar/.gitignore
    touch -d "2 hours ago" $root/libtar/.git/config $root/libtar/.git/hooks $root/libtar/.gitignore $root/libtar/.git
    startFS standardTest
fi

function mtimeTestPart1 {
    (cd $mount; find . -exec ls -ld \{\} \; > $org)    
    stopFS nook
    touch $root/beta/zz
    startFS mtimeTestPart2
}

function mtimeTestPart2 {
    (cd $mount; find . -exec ls -ld \{\} \; > $dest)
    rc=$(diff -d $org $dest | grep -v ./beta/tar00000000.tar | tr -d '[:space:]')
    if [ "$rc" != "3c3---" ]; then
        echo Failed changed mtime should affect tar mtime test! Check in $dir for more information.
        echo This test also fails, if the sort order of depthFirst is modified.
        exit
    fi    
    stopFS    
}

setup basic11 "Test last modified timestamp"
if [ $do_test ]; then
    mkdir -p $root/alfa
    mkdir -p $root/beta
    echo HEJSAN > $root/alfa/xx
    echo HEJSAN > $root/alfa/yy
    echo HEJSAN > $root/beta/zz
    echo HEJSAN > $root/beta/ww
    touch -d "2 hours ago" $root/alfa/* $root/beta/* $root/alfa $root/beta
    startFS mtimeTestPart1
fi

function checkChunkCreation {
    cat $log
    rc=$(cd $mount; find . -name "*.tar" | tr -d '[:space:]')
    if [ "$rc" != "./NNNNN/tar00000000.tar./NNNNN/tar00000001.tar./NNNNN/taz00000000.tar./taz00000000.tar" ]; then
        echo Chunks not created in the proper order! Check in $dir for more information.
        exit
    fi
    stopFS
}

setup basic12 "Test that sort order is right for proper chunk point creation"
if [ $do_test ]; then
    mkdir -p $root/NNNNN/SSSS
    dd bs=1024 count=6000 if=/dev/zero of=$root/NNNNN/RRRRR > /dev/null 2>&1
    dd bs=1024 count=1 if=/dev/zero of=$root/NNNNN/SSSS/SSSS > /dev/null 2>&1
    dd bs=1024 count=6000 if=/dev/zero of=$root/NNNNN/iiii > /dev/null 2>&1
    startFS checkChunkCreation "-v"
fi

setup basic13 "Test paths with spaces"
if [ $do_test ]; then
    mkdir -p "$root/alfa beta/gamma"
    mkdir -p "$root/alfa beta/gam ma"
    echo HEJSAN > "$root/alfa beta/gamma/delta"
    echo HEJSAN > "$root/alfa beta/gamma/del ta"
    echo HEJSAN > "$root/alfa beta/gam ma/del ta"
    startFS standardTest
fi

function checkExtractFile {
    untar "alfa beta/gamma/del ta"
    diff "$root/alfa beta/gamma/del ta" "$check/alfa beta/gamma/del ta"
    if [ "$?" != "0" ]; then
        echo Could not extract single file! Check in $dir for more information.
        exit
    fi
    stopFS
}

setup basic14 "Test single file untar"
if [ $do_test ]; then
    mkdir -p "$root/alfa beta/gamma"
    mkdir -p "$root/alfa beta/gam ma"
    echo HEJSAN1 > "$root/alfa beta/gamma/delta"
    echo HEJSAN2 > "$root/alfa beta/gamma/del ta"
    echo HEJSAN3 > "$root/alfa beta/gam ma/del ta"
    echo HEJSAN4 > "$root/alfa beta/del ta"
    echo HEJSAN5 > "$root/alfa beta/delta"
    echo HEJSAN6 > "$root/delta"
    startFS checkExtractFile
fi

function percentageTest {
    untar
    checkdiff
    checkls-ld
    # This error can pop up because of accidental use of printf in perl code.
    # The percentage in the file name will become a printf command.
    $THIS_DIR/integrity-test.sh -dd "$dir" "$root" "$mount"
    if [ $? -ne 0 ]; then
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
    if [[ $(find "$dir/test_root.txt" -type f -size +200c 2>/dev/null) ]] || 
       [[ $(find "$dir/test_mount.txt" -type f -size +200c 2>/dev/null) ]] ; then
        echo Percentage in filename was not handled properly! Check in $dir for more information.
        exit
    fi
    stopFS nook
}

setup basic15 "Test paths with percentages in them"
if [ $do_test ]; then
    mkdir -p "$root/alfa"
    echo HEJSAN > "$root/alfa/p%25e2%2594%259c%25c3%2591vensf%25e2%2594%259c%25e2%2595%25a2.jpg"
    startFS percentageTest
fi

setup basic16 "Test small/medium/large tar files"
if [ $do_test ]; then
    for i in s{1..199}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=60 > /dev/null 2>&1
    done
    for i in m{1..49}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=400 > /dev/null 2>&1
    done
    for i in l{1..2}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=10240 > /dev/null 2>&1
    done
    startFS standardTest 
fi

function noAvalancheTestPart1 {
    (cd $mount; find . -exec ls -ld \{\} \; > $org)    
    stopFS nook
    dd if=/dev/zero of="$root/s200" bs=1024 count=60 > /dev/null 2>&1
    startFS noAvalancheTestPart2
}

function noAvalancheTestPart2 {
    (cd $mount; find . -exec ls -ld \{\} \; > $dest)
    rc=$(diff -d $org $dest | grep -v ./tar00000001.tar | grep -v ./taz00000000.tar | tr -d '[:space:]')
    if [ "$rc" != "7,8c7,8---" ]; then
        echo ++$rc++
        echo Failed no avalanche test should only affect a single tar! $ord $dest
        exit
    fi    
    stopFS    
}

setup basic17 "Test that added file does not create avalanche"
if [ $do_test ]; then
    for i in s{1..199}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=60 > /dev/null 2>&1
    done
    for i in m{1..49}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=400 > /dev/null 2>&1
    done
    for i in l{1..2}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=10240 > /dev/null 2>&1
    done
    startFS noAvalancheTestPart1
fi

setup libtar1 "Mount of libtar extract all"
if [ $do_test ]; then
    cp -a libtar $root
    startFS standardTest
fi

function compareTwo {
    rc=$($THIS_DIR/compare.sh "$root" "$mountreverse" | grep -v $'< .\t' | grep -v $'> .\t')
    if [ "$rc" != $'1c1\n---' ]; then
        echo xx"$rc"xx
        echo Unexpected diff after forward and then reverse!
        exit
    fi
    stopTwoFS
}

setup reverse1 "Forward mount of libtar, Reverse mount back!"
if [ $do_test ]; then
    cp -a libtar $root
    startTwoFS compareTwo
fi


echo All tests succeeded!
#rm -rf $tmpdir
