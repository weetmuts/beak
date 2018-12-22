#!/bin/bash
#
#
#    Copyright (C) 2016-2018 Fredrik Öhrström
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

# The test directory, for example: /test/beak_testXXXXX/basic01/
dir=""

# The root contains a set of files and directories
# to be backed up using beak.
root=""

# Mount a virtual beakfs with tars here:
mount=""
mountreverse=""

# Store a beakfs here:
store=""

checkpack=""

# Recreate the files here. Typically $check should be identical to $root.
check=""

log=""
org=""
dest=""
do_test=""

subdir=""

# beakfs: Parameter to the test, where to find the beak file system.
# Usually either $mount or $store
beakfs=""
# if_test_fail_msg: Message tuned to the failure of the test.
if_test_fail_msg=""

BEAK=$1

if [ "$BEAK" = "" ]; then
    echo First argument must be the binary to test!
    exit 1
fi


test=$2
gdb=$3

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIS_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

if [ "$1" == "list" ]; then
    grep '^setup ' $THIS_SCRIPT | grep -v grep | cut -f 2- -d ' '
    exit
fi

function setup {
    do_test=""
    if [ -z "$test" ] || [ "$test" == "$1" ]; then
        do_test="yes"
        echo $1: $2
        dir=$tmpdir/$1
        # root contains the simulated files to be backed up / archived.
        root=$dir/Root
        # mount = mount point for the beakfs
        mount=$dir/Mount
        # mountreverse = hopefully the same as root!
        mountreverse=$dir/MountReverse
        # store contains the same files as in mount, but real, not virtual.
        store=$dir/Store

        check=$dir/Check
        packed=$dir/Packed
        log=$dir/log.txt
        logreverse=$dir/logreverse.txt
        org=$dir/org.txt
        dest=$dir/dest.txt
        diff=$dir/diff.txt
        mkdir -p $root
        mkdir -p $mount
        mkdir -p $mountreverse
        mkdir -p $store
        mkdir -p $check
        mkdir -p $packed
    fi
}

function performStore {
    extra="$1"
    if [ -z "$test" ]; then
        # Normal test execution, execute the store
        eval "${BEAK} store $extra $root $store > $log"
    else
        if [ -z "$gdb" ]; then
            ${BEAK} store --log=all $extra $root $store 2>&1 | tee $log
        else
            gdb -ex=r --args ${BEAK} store -f $extra $root $store
        fi
    fi
}

function performReStore {
    extra="$1"
    if [ -z "$test" ]; then
        # Normal test execution, execute the store
        eval "${BEAK} restore $extra "${store}${subdir}" $check > $log"
    else
        if [ -z "$gdb" ]; then
            ${BEAK} restore --log=all $extra $store $check 2>&1 | tee $log
        else
            gdb -ex=r --args ${BEAK} restore -f $extra $store $check
        fi
    fi
}

function performDiff {
    extra="$1"
    if [ -z "$test" ]; then
        # Normal test execution, execute the store
        eval "${BEAK} diff $extra ${root} ${store} > $diff"
    else
        if [ -z "$gdb" ]; then
            ${BEAK} diff --log=all $extra ${root} ${store} 2>&1 | tee $diff
        else
            gdb -ex=r --args ${BEAK} diff -f $extra ${root} ${store}
        fi
    fi
}

function startMountTest {
    run="$1"
    extra="$2"
    if [ -z "$test" ]; then
        # Normal test execution, spawn the beakfs fuse daemon
        eval "${BEAK} bmount $extra $root $mount > $log"
        # Then run the test
        ${run}
    else
        if [ -z "$gdb" ]; then
            # Running a chosen test, we want to see the debug output from beak.
            # Spawn the daemon in foreground, delay start of the test by 2 seconds.
            (sleep 2; eval ${run}) &
            eval "${BEAK} bmount -f $extra $root $mount 2>&1 | tee $log &"
        else
            # Running a chosen test in gdb. The gdb session must be in the foreground.
            # Delay start of the test by 3 seconds.
            (sleep 3; eval ${run}) &
            eval "gdb -ex=r --args ${BEAK} bmount -f $extra $root $mount"
        fi
    fi
}

function startMountTestArchive {
    run="$1"
    extra="$2"
    if [ -z "$test" ]; then
        ${BEAK} mount $extra $packed $check > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            ${BEAK} mount -f $extra $packed $check 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args ${BEAK} mount -f $extra $packed $check
        fi
    fi
}

function startMountTestExpectFail {
    run="$1"
    extra="$2"
    env="$3"
    if [ -z "$test" ]; then
        "$env" ${BEAK} bmount $extra $root $mount > $log 2>&1
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 2; eval ${run}) &
            "$env" ${BEAK} bmount -f $extra $root $mount 2>&1 | tee $log &
        else
            (sleep 3; eval ${run}) &
            gdb -ex=r --args "$env" ${BEAK} bmount -f $extra $root $mount
        fi
    fi
}

function stopMount {
    (cd $mount; find . -exec ls -ld \{\} \; >> $log)
    fusermount -u $mount
    if [ -n "$test" ]; then
        sleep 2
    fi
}

function stopMountArchive {
    (cd $check; find . -exec ls -ld \{\} \; >> $log)
    fusermount -u $check
    if [ -n "$test" ]; then
        sleep 2
    fi
}

function startTwoFS {
    run="$1"
    extra="$2"
    extrareverse="$3"
    if [ -z "$test" ]; then
        ${BEAK} bmount $extra $root $mount > $log
        sleep 2
        ${BEAK} mount $extrareverse $mount $mountreverse > $log
        ${run}
    else
        if [ -z "$gdb" ]; then
            (sleep 4; eval ${run}) &
            ${BEAK} bmount $extra $root $mount 2>&1 | tee $log &
            ${BEAK} mount -f $extrareverse $mount $mountreverse 2>&1 | tee $logreverse &
        else
            (sleep 5; eval ${run}) &
            ${BEAK} bmount $extra $root $mount > $log
            sleep 2
            gdb -ex=r --args ${BEAK} mount -f $extrareverse $mount $mountreverse
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
    (cd "$check"; $THIS_DIR/scripts/restore.sh x "$1")
}

function pack {
    (cd "$dir"; $THIS_DIR/scripts/pack.sh gzip "$mount" "$packed") >> "$log" 2>&1
}

function untarpacked {
    (cd "$check"; $THIS_DIR/scripts/restore.sh xa "$1" "$2")
}

function checkdiff {
    diff -rq $root$1 $check
    if [ $? -ne 0 ]; then
        echo "$if_test_fail_msg"
        echo Failed diff for $1! Check in $dir for more information.
        exit
    fi
}

function checklsld {
    (cd $root$1; find . ! -path . -exec ls -ld --time-style='+%Y-%m-%d %H:%M:%S.%N %s' \{\} \; > $org)
    (cd $check; find . ! -path . -exec ls -ld --time-style='+%Y-%m-%d %H:%M:%S.%N %s' \{\} \; > $dest)
    diff $org $dest
    if [ $? -ne 0 ]; then
        echo "$if_test_fail_msg"
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
}

function checklsld_no_nanos {
    (cd $root$1; find . ! -path . -exec ls -ld --time-style='+%Y-%m-%d %H:%M:%S %s' \{\} \; > $org)
    (cd $check; find . ! -path . -exec ls -ld --time-style='+%Y-%m-%d %H:%M:%S %s' \{\} \; > $dest)
    diff $org $dest
    if [ $? -ne 0 ]; then
        echo "$if_test_fail_msg"
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
}

function standardStoreUntarTest {
    if_test_fail_msg="Store untar test failed: "
    untar "$store"
    checkdiff
    checklsld_no_nanos
}

function standardStoreRestoreTest {
    if_test_fail_msg="Store unstor test failed: "
    performReStore
    checkdiff
    checklsld
}

function compareStoreAndMount {
    diff -rq $store $mount
    if [ $? -ne 0 ]; then
        echo Store and Mount generated different beak filesystems! Check in $dir for more information.
        exit
    fi
}

function standardTest {
    if_test_fail_msg="Mount test failed: "
    untar "$beakfs"
    checkdiff
    checklsld_no_nanos
}

function standardPackedTest {
    pack
    untarpacked "$packed"
    checkdiff
    checklsld_no_nanos
    stopMount
    echo OK
}

function fifoStoreUntarTest {
    if_test_fail_msg="Store untar fifo test failed: "
    untar "$store"
    checklsld_no_nanos
}

function fifoStoreUnStoreTest {
    if_test_fail_msg="Store unstore fifo test failed: "
    performReStore
    checklsld
}

function fifoTest {
    if_test_fail_msg="Mount untar fifo test failed: "
    untar "$mount"
    checklsld_no_nanos
    stopMount
    echo OK
}

function cleanCheck {
    find "$check" -type d ! -perm /u+w -exec chmod u+w \{\} \;
    rm -rf "$check"
    mkdir -p "$check"
}

setup basic01 "Short simple file (fits in 100 char name)"
if [ $do_test ]; then
    echo HEJSAN > $root/hello.txt
    chmod 400 $root/hello.txt

    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest --tarheader=full
    compareStoreAndMount
    stopMount
    echo OK
fi

setup basic02 "Medium path name"
if [ $do_test ]; then
    tmp=$root/aaaa/bbbb/cccc/dddd/eeee/ffff/gggg/hhhh/iiii/jjjj/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    mkdir -p $tmp
    echo HEJSAN > $tmp'/012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789.txt'
    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest --tarheader=full
    compareStoreAndMount
    stopMount
    echo OK
fi

setup basic03 "Long file name"
if [ $do_test ]; then
    mkdir -p $root/workspace/InvokeDynamic/opts
    echo HEJSAN > $root'/workspace/InvokeDynamic/opts/sun_nio_cs_UTF_8$Encoder_encodeArrayLoop_Ljava_nio_CharBuffer;Ljava_nio_ByteBuffer;_Ljava_nio_charset_CoderResult;.xml'
    tmp=$root/aaaa/bbbb/cccc/dddd/eeee/ffff/gggg/hhhh/iiii/jjjj/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy/zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
    mkdir -p $tmp
    echo HEJSAN > $tmp'/0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345.txt'
    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest --tarheader=full
    compareStoreAndMount
    stopMount
    echo OK
fi

setup basic04 "Exactly 100 char file name"
if [ $do_test ]; then
    tmp=$root/test/test
    mkdir -p $tmp
    echo HEJSAN > $tmp'/0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789'
    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest --tarheader=full
    compareStoreAndMount
    stopMount
    echo OK
fi

setup basic05 "Long paths cause problems"
if [ $do_test ]; then
    mkdir -p $root/BhlcuNTyTvLedMdLYqDeSySKkGCajOLG/JelKMOzorxaHRRYilhHCH/zGtUkDjJrpaYruHVsh
    echo Hejsan > $root/BhlcuNTyTvLedMdLYqDeSySKkGCajOLG/JelKMOzorxaHRRYilhHCH/zGtUkDjJrpaYruHVsh/zTeEgnbHEROQBZhnLzfkSOWkAu
    echo Hejsan > $root/BhlcuNTyTvLedMdLYqDeSySKkGCajOLG/JelKMOzorxaHRRYilhHCH/AijIwubbgq
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

setup simplediff "Simple diff"
if [ $do_test ]; then
    mkdir -p $root/Alfa/Beta
    mkdir -p $root/Alfa/Gamma
    echo HEJSAN > $root/Alfa/Beta/gurka
    echo HEJSAN > $root/Alfa/Gamma/banan
    echo HEJSAN > $root/Alfa/Gamma/toppen
    performStore
    performDiff
    CHECK=$(cat $diff)
    if [ ! "$CHECK" = "" ]; then
        echo Failed beak diff! Expected no change. Check in $dir for more information.
        exit
    fi
    echo SVEJSAN > $root/Alfa/Gamma/gurka
    performDiff
    CHECK=$(grep ":  " $diff | tr -d ' \n')
    if [ ! "$CHECK" = "added:/Alfa/Gamma/gurka" ]; then
        cat $diff
        echo CHECK=\"${CHECK}\"
        echo Failed beak diff! Expected one added. Check in $dir for more information.
        exit
    fi
    echo SVEJSAN > $root/Alfa/Gamma/banan
    performDiff
    CHECK=$(grep ":  " $diff | tr -d ' \n' )
    if [ ! "$CHECK" = "changed:/Alfa/Gamma/bananadded:/Alfa/Gamma/gurka" ]; then
        cat $diff
        echo CHECK=\"${CHECK}\"
        echo Failed beak diff! Expected one added and one changed. Check in $dir for more information.
        exit
    fi
    rm $root/Alfa/Beta/gurka
    performDiff
    CHECK=$(grep ":  " $diff | tr -d ' \n' )
    if [ ! "$CHECK" = "changed:/Alfa/Gamma/bananadded:/Alfa/Gamma/gurkaremoved:/Alfa/Beta/gurka" ]; then
        cat $diff
        echo CHECK=\"${CHECK}\"
        echo Failed beak diff! Expected one added, one removed and one changed. Check in $dir for more information.
        exit
    fi
    touch $root/Alfa/Beta/toppen
    performDiff
    CHECK=$(grep ":  " $diff | tr -d ' \n' )
    if [ ! "$CHECK" = "changed:/Alfa/Gamma/bananadded:/Alfa/Gamma/gurkaremoved:/Alfa/Beta/gurka" ]; then
        cat $diff
        echo CHECK=\"${CHECK}\"
        echo Failed beak diff! Expected one added, one removed, one changed and one permission. Check in $dir for more information.
        exit
    fi
    echo OK
fi

setup splitparts "Split large file into multiple small parts"
if [ $do_test ]; then
    dd if=/dev/urandom of=$root'/largefile' count=71 bs=1023 > /dev/null 2>&1
    performStore "-ta 25K -ts 66K --tarheader=full"
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest "-ta 25K -ts 66K --tarheader=full"
    compareStoreAndMount
    stopMount
    echo OK
fi

setup splitpartslongname "Split large file with long file name into multiple small parts"
if [ $do_test ]; then
    dir=$root/'aaaa/bbbb/cccc/dddd/eeee/ffff/gggg/hhhh/iiii/jjjj/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy/zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz'
    filename="${dir}/0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345.txt"
    mkdir -p "$dir"
    dd if=/dev/urandom of=$filename count=71 bs=1023 > /dev/null 2>&1
    performStore "-ta 25K -ts 66K"
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest "-ta 25K -ts 66K"
    compareStoreAndMount
    stopMount
    echo OK
fi

setup splitmoreparts "Split larger file into multiple small parts"
if [ $do_test ]; then
    dd if=/dev/urandom of=$root'/largefile' count=3271 bs=1023 > /dev/null 2>&1
    performStore "-ta 100K -ts 213K"
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest "-ta 100K -ts 213K"
    compareStoreAndMount
    stopMount
    echo OK
fi

setup splitmanymoreparts "Split larger file into many many small parts"
if [ $do_test ]; then
    dd if=/dev/urandom of=$root'/largefile' count=8192 bs=2048 > /dev/null 2>&1
    performStore "-ta 15K -ts 37K"
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest "-ta 15K -ts 37K"
    compareStoreAndMount
    stopMount
    echo OK
fi

setup splitbasedoncontent "Split large file based on content"
if [ $do_test ]; then
    dd if=/dev/urandom of=$root'/largefile.vdi' count=8192 bs=2048 > /dev/null 2>&1
    performStore "-ta 40K -ts 100K --contentsplit '*.vdi'"
    standardStoreUntarTest
    echo OK
fi

setup symlink "Symbolic link"
if [ $do_test ]; then
    echo HEJSAN > $root/test
    ln -s $root/test $root/link
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

setup symlink_long "Symbolic link to long target"
if [ $do_test ]; then
    tmp=$root/01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
    echo HEJSAN > $tmp
    ln -s $tmp $root/link
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

setup hardlink "Hard link"
if [ $do_test ]; then
    echo HEJSAN > $root/test1
    ln $root/test1 $root/link1
    mkdir -p $root/alfa/beta
    echo HEJSAN > $root/alfa/beta/test2
    ln $root/alfa/beta/test2 $root/alfa/beta/link2
    ln $root/alfa/beta/test2 $root/linkdeep
    mkdir -p $root/gamma/epsilon
    ln $root/alfa/beta/test2 $root/gamma/epsilon/test3
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

setup longhardlink "Long hard link"
if [ $do_test ]; then
    mkdir -p $root/Work/beak/3rdparty/openssl-1.0.2-winapi/.git/objects/pack
    echo HEJSAN > $root/Work/beak/3rdparty/openssl-1.0.2-winapi/.git/objects/pack/pack-21499a1067865c380bfb261287724242a1fe2e9c.idx
    mkdir -p $root/Work/beak/3rdparty/openssl-1.0.2-arm/.git/objects/pack
    ln $root/Work/beak/3rdparty/openssl-1.0.2-winapi/.git/objects/pack/pack-21499a1067865c380bfb261287724242a1fe2e9c.idx $root/Work/beak/3rdparty/openssl-1.0.2-arm/.git/objects/pack/pack-21499a1067865c380bfb261287724242a1fe2e9c.idx
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

setup fifo "FIFO"
if [ $do_test ]; then
    mkfifo $root/fifo1
    mkfifo $root/fifo2
    performStore
    fifoStoreUntarTest
    cleanCheck
    fifoStoreUnStoreTest
    cleanCheck
    startMountTest fifoTest
fi

function filterCheck {
    F=$(cd $check; find . -printf "%p ")
    if [ "$F" != ". ./Beta ./Beta/delta " ]; then
        echo Failed filter test $1! Check in $dir for more information.
        exit
    fi
}

function storeUntarFilterTest {
    untar "$store"
    filterCheck
}

function storeUnStoreFilterTest {
    performReStore
    filterCheck
}

function mountFilterTest {
    untar "$mount"
    filterCheck
    stopMount
    echo OK
}

setup filter "Include exclude"
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    mkdir -p $root/Beta
    echo HEJSAN > $root/Beta/delta
    echo HEJSAN > $root/BetaDelta
    performStore "-i Beta/**"
    storeUntarFilterTest
    cleanCheck
    storeUnStoreFilterTest
    cleanCheck
    startMountTest mountFilterTest "-i Beta/**"
fi

setup partial_extraction "Extract a subdirectory in the backup!"
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    echo HEJSAN > $root/Beta
    mkdir -p $root/Gamma
    echo HEJSAN > $root/Gamma/Delta
    echo HEJSAN > $root/Gamma/Tau
    performStore
    if_test_fail_msg="Store untar test failed: "
    untar "$store/Gamma"
    checkdiff /Gamma
    checklsld_no_nanos /Gamma
    cleanCheck
    if_test_fail_msg="Store unstor test failed: "
    subdir="/Gamma"
    performReStore
    checkdiff /Gamma
    checklsld /Gamma
    cleanCheck
    subdir=""
    echo OK
fi

setup write_protected_paths "Extract write protected directories."
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    echo HEJSAN > $root/Beta
    mkdir -p $root/Gamma
    echo HEJSAN > $root/Gamma/Delta
    echo HEJSAN > $root/Gamma/Tau
    chmod a-w $root/Gamma
    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest --tarheader=full
    compareStoreAndMount
    stopMount
    echo OK
fi

setup update_write_protected_dirs "Restore new file into write protected directories."
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    echo HEJSAN > $root/Beta
    mkdir -p $root/Gamma
    echo HEJSAN > $root/Gamma/Delta
    echo HEJSAN > $root/Gamma/Tau
    chmod a-w $root/Gamma
    performStore
    standardStoreRestoreTest
    chmod u+w $root/Gamma
    echo HEJSAN > $root/Gamma/Ypsilon
    chmod u-w $root/Gamma
    performStore
    standardStoreRestoreTest
    cleanCheck
    # No check of mount since we want to test our own restore write code.
    # The mount will always render the write-protected file properly.
    echo OK
fi

setup update_write_protected_file "Restore write protected file."
if [ $do_test ]; then
    echo HEJSAN > $root/Alfa
    echo HEJSAN > $root/Beta
    mkdir -p $root/Gamma
    echo HEJSAN > $root/Gamma/Delta
    chmod a-w $root/Gamma/Delta
    performStore
    standardStoreRestoreTest
    chmod u+w $root/Gamma/Delta
    echo HEJSAN > $root/Gamma/Delta
    chmod u-w $root/Gamma/Delta
    performStore
    standardStoreRestoreTest
    cleanCheck
    # No check of mount since we want to test our own restore write code.
    # The mount will always render the write-protected file properly.
    echo OK
fi

function devTest {
    $THIS_DIR/scripts/integrity-test.sh -dd "$dir" -f "! -path '*shm*'" /dev "$beakfs"
    if [ $? -ne 0 ]; then
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
}

setup devices "block and character devices (ie the whole /dev directory)"
if [ $do_test ]; then
    # Testing char and block devices are difficult since they
    # require you to be root. So lets just mount /dev and
    # just list the contents of the tars!
    root=/dev
    performStore "--tarheader=full -x '/vboxusb' -x '/shm/**'"
    beakfs="$store"
    devTest
    beakfs="$mount"
    startMountTest devTest "--tarheader=full -x '/vboxusb' -x '/shm/**'"
    compareStoreAndMount
    stopMount
    echo OK
fi

setup tar_ordering "check that nothing gets between the directory and its contents"
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

    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

function mtimeTestPart1 {
    (cd $mount; find . -exec ls -ld \{\} \; > $org)
    stopMount
    touch -d "2015-03-03 03:03:03.1235" $root/beta/zz
    startMountTest mtimeTestPart2 "-d 1"
}

function mtimeTestPart2 {
    (cd $mount; find . -exec ls -ld \{\} \; > $dest)
    cat $org | sed 's/.02_.*//' > ${org}.1
    cat $dest | sed 's/.02_.*//' > ${dest}.1
    rc=$(diff -d ${org}.1 ${dest}.1)

    if [ "$rc" != "" ]; then
        echo "****$rc****"
        echo Comparison should be empty since the nanoseconds do not show in ls -ld.
        echo Check in $dir for more information.
        exit
    fi

    cat $org  | egrep -o z02_[[:digit:]]+\.[[:digit:]]+ | sed 's/1234/1235/' > ${org}.2
    cat $dest | egrep -o z02_[[:digit:]]+\.[[:digit:]]+  > ${dest}.2
    rc=$(diff -d ${org}.2 ${dest}.2)

    if [ "$rc" != "" ]; then
        echo "****$rc****"
        echo Comparison should be empty since we adjusted the 1234 nanos to 12345
        echo and cut away the hashes that are expected to change.
        echo Check in $dir for more information.
        exit
    fi
    stopMount
}

setup mtime "Test last modified timestamp"
if [ $do_test ]; then
    mkdir -p $root/alfa
    mkdir -p $root/beta
    echo HEJSAN > $root/alfa/xx
    echo HEJSAN > $root/alfa/yy
    echo HEJSAN > $root/beta/zz
    echo HEJSAN > $root/beta/ww
    touch -d "2015-03-03 03:03:03.1234" $root/alfa/* $root/beta/* $root/alfa $root/beta
    startMountTest mtimeTestPart1 "-d 1"
    echo OK
fi


function timestampHashTest1 {
    rc1=$(ls $mount/TJO/s*.tar)
    stopMount
    touch -d "2015-03-03 03:03:03.1235" $root/TJO/alfa
    startMountTest timestampHashTest2
}

function timestampHashTest2 {
    rc2=$(ls $mount/TJO/s*.tar)
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
    stopMount
}

setup mtime_hash "check that timestamps influence file hash"
if [ $do_test ]; then
    mkdir -p $root/TJO
    touch -d "2015-03-03 03:03:03.1234" $root/TJO/alfa
    startMountTest timestampHashTest1
    echo OK
fi

setup paths_with_spaces "Test paths with spaces"
if [ $do_test ]; then
    mkdir -p "$root/alfa beta/gamma"
    mkdir -p "$root/alfa beta/gam ma"
    echo HEJSAN > "$root/alfa beta/gamma/delta"
    echo HEJSAN > "$root/alfa beta/gamma/del ta"
    echo HEJSAN > "$root/alfa beta/gam ma/del ta"
    performStore
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest standardTest
    compareStoreAndMount
    stopMount
    echo OK
fi

function checkExtractFile {
    untar "$mount" "alfa beta/gamma/del ta"
    diff "$root/alfa beta/gamma/del ta" "$check/alfa beta/gamma/del ta"
    if [ "$?" != "0" ]; then
        echo Could not extract single file! Check in $dir for more information.
        exit
    fi
}

setup test_single_untar "Test single file untar"
if [ $do_test ]; then
    mkdir -p "$root/alfa beta/gamma"
    mkdir -p "$root/alfa beta/gam ma"
    echo HEJSAN1 > "$root/alfa beta/gamma/delta"
    echo HEJSAN2 > "$root/alfa beta/gamma/del ta"
    echo HEJSAN3 > "$root/alfa beta/gam ma/del ta"
    echo HEJSAN4 > "$root/alfa beta/del ta"
    echo HEJSAN5 > "$root/alfa beta/delta"
    echo HEJSAN6 > "$root/delta"
    startMountTest checkExtractFile
    stopMount
fi

function percentageTest {
    untar "$mount"
    checkdiff
    checklsld_no_nanos
    # This error can pop up because of accidental use of printf in perl code.
    # The percentage in the file name will become a printf command.
    $THIS_DIR/scripts/integrity-test.sh -dd "$dir" "$root" "$mount"
    if [ $? -ne 0 ]; then
        echo Failed file attributes diff for $1! Check in $dir for more information.
        exit
    fi
    if [[ $(find "$dir/test_root.txt" -type f -size +200c 2>/dev/null) ]] ||
       [[ $(find "$dir/test_mount.txt" -type f -size +200c 2>/dev/null) ]] ; then
        echo Percentage in filename was not handled properly! Check in $dir for more information.
        exit
    fi
}

setup paths_with_percentages "Test paths with percentages in them"
if [ $do_test ]; then
    mkdir -p "$root/alfa"
    echo HEJSAN > "$root/alfa/p%25e2%2594%259c%25c3%2591vensf%25e2%2594%259c%25e2%2595%25a2.jpg"
    performStore --tarheader=full
    standardStoreUntarTest
    cleanCheck
    standardStoreRestoreTest
    cleanCheck
    beakfs="$mount"
    startMountTest percentageTest "--tarheader=full"
    stopMount
fi

setup small_medium_large "Test small/medium/large tar files"
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
    beakfs="$mount"
    startMountTest standardTest
    stopMount
    echo OK
fi

function expectCaseConflict {
    if [ "$?" == "0" ]; then
        echo Expected beak to fail startup!
        stopMount
        exit
    fi
}

setup case_conflict_detected "Test that case conflicts are detected"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    mkdir -p $root/alfa
    echo HEJSAN > $root/Alfa/a
    echo HEJSAN > $root/alfa/b
    startMountTestExpectFail expectCaseConflict "-d 1 -ta 0"
    echo OK
fi

setup case_conflict_hidden "Test that case conflicts can be hidden inside tars"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    mkdir -p $root/alfa
    echo HEJSAN > $root/Alfa/a
    echo HEJSAN > $root/alfa/b
    beakfs="$mount"
    startMountTest standardTest "-d 1 -ta 1G"
    stopMount
    echo OK
fi

function expectLocaleFailure {
    if [ "$?" == "0" ]; then
        echo Expected beak to fail startup!
        stopMount nook
        exit
    fi
}

setup locale_C "Test that LC_ALL=C fails"
if [ $do_test ]; then
    mkdir -p $root/Alfa
    echo HEJSAN > $root/Alfa/a
    startMountTestExpectFail expectLocaleFailure "-d 1 -ta 0" LC_ALL=en_US.UTF-8
    echo OK
fi

function txTriggerTest {
    if [ ! -f $mount/Alfa/snapshot_2016-12-30/z02_*.gz ]; then
        echo Expected the snapshot dir to be tarred! Check in $dir for more information.
        exit
    fi
    untar "$mount"
    checkdiff
    checklsld_no_nanos

    stopMount
}

setup trigger_tarred_dirs "Test -tx to trigger tarred directories"
if [ $do_test ]; then
    mkdir -p $root/Alfa/snapshot_2016-12-30
    echo HEJSAN > $root/Alfa/a
    cp -a $root/Alfa/a $root/Alfa/snapshot_2016-12-30
    startMountTest txTriggerTest "-tx 'snapshot*'"
    echo OK
fi

function noAvalancheTestPart1 {
    (cd $mount; find . -exec ls -ld \{\} \; > $org)
    stopMount
    dd if=/dev/zero of="$root/s200" bs=1024 count=60 > /dev/null 2>&1
    touch -d "2015-03-03 03:03:03.1235" "$root/s200"
    startMountTest noAvalancheTestPart2 "-ta 1M -tr 1M"
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

    stopMount
}

setup no_avalanche "Test that added file does not create avalanche"
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
    startMountTest noAvalancheTestPart1 "-ta 1M -tr 1M"
    echo OK
fi

setup bulktest1 "Mount of generated bulk extract all default settings"
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 5 10
    beakfs="$mount"
    startMountTest standardTest
    stopMount
    echo OK
fi

setup bulktest2 "Mount of generated bulk, pack using xz, decompress and untar."
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 5 10
    startMountTest standardPackedTest
    echo OK
fi

function expectOneBigR01Tar {
    untar "$mount"
    checkdiff
    checklsld_no_nanos
    num=$(find $mount -name "s02*.tar" | wc --lines)
    if [ "$num" != "1" ]; then
        echo Expected a single big s02 tar! Check in $dir for more information.
        exit
    fi
    stopMount
}

setup bulktest3 "Mount of generated bulk -d 1 -ta 1G"
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 5 10
    startMountTest expectOneBigR01Tar "-d 1 -ta 1G"
    echo OK
fi

function expect8R01Tar {
    untar "$mount"
    checkdiff
    checklsld_no_nanos
    num=$(find $mount -name "s02*.tar" | wc --lines)
    if [ "$num" != "8" ]; then
        echo Expected 8 s02 tar! Check in $dir for more information.
        exit
    fi
    stopMount
}

setup bulktest4 "Mount of generated bulk -d 1 -ta 1M -tr 1G"
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 5 10
    startMountTest expect8R01Tar "-d 1 -ta 1M -tr 1G"
    echo OK
fi

function compareTwo {
    rc=$($THIS_DIR/scripts/compare.sh --nodirs "$root" "$mountreverse")
    if [ "$rc" != "" ]; then
        echo xx"$rc"xx
        echo Unexpected diff after forward and then reverse!
        exit
    fi
    stopTwoFS
}

setup reverse1 "Forward mount of libtar, Reverse mount back!"
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 5 10
    startTwoFS compareTwo "" "-p @0"
    echo OK
fi

function pointInTimeTestPart1 {
    cp -r "$mount"/* "$packed"
    chmod -R u+w "$packed"/*
    stopMount nook
    dd if=/dev/zero of="$root/s200" bs=1024 count=60 > /dev/null 2>&1
    startMountTest pointInTimeTestPart2
}

function pointInTimeTestPart2 {
    cp -r "$mount"/* "$packed"
    chmod -R u+w "$packed"/*
    stopMount nook
    startMountTestArchive pointInTimeTestPart3 "-p @0"
}

function pointInTimeTestPart3 {
    if [ ! -f "$check/s200" ]; then
        echo Error s200 should be there since we mount the latest pointInTime.
        echo Check in $dir for more information.
        exit
    fi
    stopMountArchive
    startMountTestArchive pointInTimeTestPart4 "-p @1"
    echo OK
}

function pointInTimeTestPart4 {
    if [ -f "$check/s200" ]; then
        echo Error s200 should NOT be there since we mount the previous pointInTime.
        echo Check in $dir for more information.
        exit
    fi
    stopMountArchive
}

setup points_in_time "Test that pointInTimes work"
if [ $do_test ]; then
    ./scripts/generate_filesystem.sh $root 3
    startMountTest pointInTimeTestPart1
    echo OK
fi

#setup diff1 "Compare directories!"
#if [ $do_test ]; then
#    mkdir -p "$root/alfa beta/gamma"
#    mkdir -p "$check/alfa beta/gamma"
#    echo HEJSAN1 > "$root/alfa beta/gamma/delta"
#    echo HEJSAN2 > "$root/alfa beta/gamma/x"
#    echo HEJSAN3 > "$root/alfa beta/gamma/y"
#    cp -a "$root/"* "$check"
#    echo HEJSAN1 > "$check/alfa beta/z"
#    rm "$check/alfa beta/gamma/x"
#    ./${prefix}/diff "$root" "$check"
#fi

echo All tests succeeded!
