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

tmpdir=$(mktemp -d /tmp/beak_testXXXXXXXX)
dir=""
root=""
mount=""
mountreverse=""
checkpack=""
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
        packed=$dir/Packed
        log=$dir/log.txt
        logreverse=$dir/logreverse.txt
        org=$dir/org.txt
        dest=$dir/dest.txt
        mkdir -p $root
        mkdir -p $mount
        mkdir -p $mountreverse
        mkdir -p $check
        mkdir -p $packed
    fi
}

function startFS {
    run="$1"
    extra="$2"
    if [ -z "$test" ]; then
        ./build/beak mount $extra $root $mount > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            ./build/beak mount -d $extra $root $mount 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args ./build/beak mount -d $extra $root $mount 
        fi        
    fi        
}

function startFSArchive {
    run="$1"
    extra="$2"
    if [ -z "$test" ]; then
        ./build/beak mount $extra $packed $check > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            ./build/beak mount -d $extra $packed $check 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args ./build/beak mount -d $extra $packed $check 
        fi        
    fi        
}

function startFSExpectFail {
    run="$1"
    extra="$2"
    env="$3"
    if [ -z "$test" ]; then
        "$env" ./build/beak mount $extra $root $mount > $log 2>&1
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            "$env" ./build/beak mount -d $extra $root $mount 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args "$env" ./build/beak mount -d $extra $root $mount 
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

function stopFSArchive {
    (cd $mount; find . -exec ls -ld \{\} \; >> $log)
    fusermount -u $check
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
        ./build/beak mount $extra $root $mount > $log
        sleep 2
        ./build/beak mount $extrareverse $mount $mountreverse > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 4; eval ${run}) &
            ./build/beak mount $extra $root $mount 2>&1 | tee $log &
            ./build/beak mount -d $extrareverse $mount $mountreverse 2>&1 | tee $logreverse &
        else
            (sleep 5; eval ${run}) &
            ./build/beak mount $extra $root $mount > $log
            sleep 2
            gdb -ex=r --args ./build/beak mount -d $extrareverse $mount $mountreverse 
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

function pack {
    (cd "$dir"; $THIS_DIR/pack.sh gzip "$mount" "$packed") >> "$log" 2>&1
}

function untarpacked {
    (cd "$check"; $THIS_DIR/untar.sh xa "$packed" "$1")
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

function standardPackedTest {
    pack
    untarpacked
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
    $THIS_DIR/integrity-test.sh -dd "$dir" -f "! -path '*shm*' -a ! -path '*tty*'" /dev "$mount"
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

setup basic07 "Hard link"
if [ $do_test ]; then
    echo HEJSAN > $root/test1
    ln $root/test1 $root/link1
    mkdir -p $root/alfa/beta
    echo HEJSAN > $root/alfa/beta/test2
    ln $root/alfa/beta/test2 $root/alfa/beta/link2
    ln $root/alfa/beta/test2 $root/linkdeep
    mkdir -p $root/gamma/epsilon
    ln $root/alfa/beta/test2 $root/gamma/epsilon/test3
    startFS standardTest
fi

setup basic08 "FIFO"
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

setup basic09 "Include exclude"
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    mkdir -p $root/Beta
    echo HEJSAN > $root/Beta/gamma
    echo HEJSAN > $root/Beta/delta
    echo HEJSAN > $root/BetaDelta
    startFS filterTest "-i Beta/ -x mm"
fi

setup basic10 "block and character devices (ie the whole /dev directory)"
if [ $do_test ]; then
    # Testing char and block devices are difficult since they
    # require you to be root. So lets just mount /dev and
    # just list the contents of the tars!
    root=/dev
    startFS devTest "-x shm -x tty -d 0"
fi

setup basic11 "check that nothing gets between the directory and its contents"
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
    touch -d "2015-03-03 03:03:03.1235" $root/beta/zz
    startFS mtimeTestPart2
}

function mtimeTestPart2 {
    (cd $mount; find . -exec ls -ld \{\} \; > $dest)
    cat $org | sed 's/.01_00.*//' > ${org}.1
    cat $dest | sed 's/.01_00.*//' > ${dest}.1
    rc=$(diff -d ${org}.1 ${dest}.1)

    if [ "$rc" != "" ]; then
        echo "****$rc****"
        echo Comparison should be empty since the nanoseconds do not show in ls -ld.
        exit
    fi

    cat $org  | sed '/\(alfa\|\.\/z01\)/! s/1234.*/1235/' > ${org}.2
    cat $dest | sed '/\(alfa\|\.\/z01\)/! s/1235.*/1235/' > ${dest}.2
    rc=$(diff -d ${org}.2 ${dest}.2)
    
    if [ "$rc" != "" ]; then
        echo "****$rc****"
        echo Comparison should be empty since we adjusted the 1234 nanos to 12345
        echo and cut away the hashes that are expected to change.
        exit
    fi
    stopFS
}

setup basic12 "Test last modified timestamp"
if [ $do_test ]; then
    mkdir -p $root/alfa
    mkdir -p $root/beta
    echo HEJSAN > $root/alfa/xx
    echo HEJSAN > $root/alfa/yy
    echo HEJSAN > $root/beta/zz
    echo HEJSAN > $root/beta/ww
    touch -d "2015-03-03 03:03:03.1234" $root/alfa/* $root/beta/* $root/alfa $root/beta
    startFS mtimeTestPart1
fi


function timestampHashTest1 {
    rc1=$(ls $mount/TJO/r*.tar)
    stopFS nook
    touch -d "2015-03-03 03:03:03.1235" $root/TJO/alfa
    startFS timestampHashTest2
}

function timestampHashTest2 {
    rc2=$(ls $mount/TJO/r*.tar)
    if [ "$rc1" = "$rc2" ]; then
        echo "$rc1"
        echo Change in timestamp should change the virtual tar file name!
        exit
    fi
    rcc1=$(echo "$rc1" | sed 's/.*_1024_\(.*\)_0.tar/\1/')    
    rcc2=$(echo "$rc2" | sed 's/.*_1024_\(.*\)_0.tar/\1/')
    if [ "$rcc1" = "$rcc2" ]; then
        echo The hashes should be different!
        echo **$rcc1** **$rcc2**
        echo Check in $dir for more information.
        exit
    fi
    stopFS
}

setup basic13 "check that timestamps influence file hash"
if [ $do_test ]; then
    mkdir -p $root/TJO
    touch -d "2015-03-03 03:03:03.1234" $root/TJO/alfa
    startFS timestampHashTest1
fi

setup basic14 "Test paths with spaces"
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

setup basic15 "Test single file untar"
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

setup basic16 "Test paths with percentages in them"
if [ $do_test ]; then
    mkdir -p "$root/alfa"
    echo HEJSAN > "$root/alfa/p%25e2%2594%259c%25c3%2591vensf%25e2%2594%259c%25e2%2595%25a2.jpg"
    startFS percentageTest
fi

setup basic17 "Test small/medium/large tar files"
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

function expectCaseConflict {
    if [ "$?" == "0" ]; then
        echo Expected beak to fail startup!
        stopFS nook
        exit
    fi
    echo OK
}

setup basic18 "Test that case conflicts are detected"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    mkdir -p $root/alfa
    echo HEJSAN > $root/Alfa/a
    echo HEJSAN > $root/alfa/b
    startFSExpectFail expectCaseConflict "-d 1 -ta 0"
fi

setup basic19 "Test that case conflicts can be hidden inside tars"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    mkdir -p $root/alfa
    echo HEJSAN > $root/Alfa/a
    echo HEJSAN > $root/alfa/b
    startFS standardTest "-d 0 -ta 1G"
fi

function expectLocaleFailure {
    if [ "$?" == "0" ]; then
        echo Expected beak to fail startup!
        stopFS nook
        exit
    fi
    echo OK
}

setup basic20 "Test that LC_ALL=C fails"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    echo HEJSAN > $root/Alfa/a
    startFSExpectFail expectLocaleFailure "-d 1 -ta 0" LC_ALL=en_US.UTF-8
fi

function txTriggerTest {
    if [ ! -f $mount/Alfa/snapshot_2016-12-30/x01_*.gz ]; then
        echo Expected the snapshot dir to be tarred! Check in $dir for more information.
        exit
    fi
    untar
    checkdiff
    checkls-ld
    
    stopFS
}

setup basic21 "Test -tx to trigger tarred directories"
if [ $do_test ]; then
    mkdir -p $root/Alfa/snapshot_2016-12-30
    echo HEJSAN > $root/Alfa/a
    cp -a $root/Alfa/a $root/Alfa/snapshot_2016-12-30
    startFS txTriggerTest "-tx snapshot_....-..-.." 
fi

function noAvalancheTestPart1 {
    (cd $mount; find . -exec ls -ld \{\} \; > $org)    
    stopFS nook
    dd if=/dev/zero of="$root/s200" bs=1024 count=60 > /dev/null 2>&1
    touch -d "2015-03-03 03:03:03.1235" "$root/s200"
    startFS noAvalancheTestPart2 "-ta 1M -tr 1M"
}

function noAvalancheTestPart2 {
    (cd $mount; find . -exec ls -ld \{\} \; > $dest)
    diff $org $dest | grep \< > ${org}.1
    diff $org $dest | grep \> > ${org}.2

    al=$(wc -l ${org}.1 | cut -f 1 -d ' ')
    bl=$(wc -l ${org}.2 | cut -f 1 -d ' ')

    if [ "$al $bl" != "2 2" ]; then
        echo Wrong number of files changed!
        echo Check in $dir for more information.
    fi

    stopFS
}

setup distribution1 "Test that added file does not create avalanche"
if [ $do_test ]; then
    for i in s{1..199}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=60 > /dev/null 2>&1
        touch -d "2015-03-03 03:03:03.1234" "$root/$i"        
    done
    for i in m{1..49}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=400 > /dev/null 2>&1
        touch -d "2015-03-03 03:03:03.1234" "$root/$i"
    done
    for i in l{1..2}; do
        dd if=/dev/zero of="$root/$i" bs=1024 count=10240 > /dev/null 2>&1
        touch -d "2015-03-03 03:03:03.1234" "$root/$i"
    done
    startFS noAvalancheTestPart1 "-ta 1M -tr 1M"
fi

setup libtar1 "Mount of libtar extract all default settings"
if [ $do_test ]; then
    cp -a libtar $root
    startFS standardTest
fi

setup libtar2 "Mount of libtar, pack using xz, decompress and untar."
if [ $do_test ]; then
    cp -a libtar $root
    startFS standardPackedTest
fi

function expectOneBigR01Tar {
    untar
    checkdiff
    checkls-ld
    num=$(find $mount -name "r01*.tar" | wc --lines)
    if [ "$num" != "1" ]; then
        echo Expected a single big r01 tar! Check in $dir for more information.
        exit
    fi        
    stopFS
}

setup options1 "Mount of libtar -d 0 -ta 1G" 
if [ $do_test ]; then
    cp -a libtar $root
    startFS expectOneBigR01Tar "-d 0 -ta 1G"
fi

function expect8R01Tar {
    untar
    checkdiff
    checkls-ld
    num=$(find $mount -name "r01*.tar" | wc --lines)
    if [ "$num" != "8" ]; then
        echo Expected 8 r01 tar! Check in $dir for more information.
        exit
    fi        
    stopFS
}

setup options2 "Mount of libtar -d 0 -ta 1M -tr 1G" 
if [ $do_test ]; then
    cp -a libtar $root
    startFS expect8R01Tar "-d 0 -ta 1M -tr 1G"
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
    startTwoFS compareTwo "" "-p @0"
fi


function generationTestPart1 {
    cp -r "$mount"/* "$packed"
    chmod -R u+w "$packed"/*
    stopFS nook
    dd if=/dev/zero of="$root/s200" bs=1024 count=60 > /dev/null 2>&1
    startFS generationTestPart2
}

function generationTestPart2 {
    cp -r "$mount"/* "$packed"
    chmod -R u+w "$packed"/*    
    stopFS nook
    startFSArchive generationTestPart3 "-p @0"
}

function generationTestPart3 {
    if [ ! -f "$check/s200" ]; then
        echo Error s200 should be there since we mount the latest generation.
        exit
    fi
    stopFSArchive nook
    startFSArchive generationTestPart4 "-p @1"
}

function generationTestPart4 {
    if [ -f "$check/s200" ]; then
        echo Error s200 should NOT be there since we mount the previous generation.
        exit
    fi
    stopFSArchive
}

setup generation1 "Test that generations work"
if [ $do_test ]; then
    cp -a libtar "$root"
    startFS generationTestPart1
fi

setup diff1 "Compare directories!"
if [ $do_test ]; then
    mkdir -p "$root/alfa beta/gamma"
    mkdir -p "$check/alfa beta/gamma"
    echo HEJSAN1 > "$root/alfa beta/gamma/delta"
    echo HEJSAN2 > "$root/alfa beta/gamma/x"
    echo HEJSAN3 > "$root/alfa beta/gamma/y"
    cp -a "$root/"* "$check"
    echo HEJSAN1 > "$check/alfa beta/z"    
    rm "$check/alfa beta/gamma/x"
    ./build/diff "$root" "$check"
fi


#echo All tests succeeded!
#rm -rf $tmpdir
