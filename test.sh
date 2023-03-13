#!/bin/bash

builddir="$1"
cwddir=$(pwd)

echo testinternals
$builddir/testinternals

beak="$builddir/beaknm"
testscripts=$(ls tests/test*.sh)
testoutput=$(pwd)/test_output

rm -rf $testoutput
mkdir -p $testoutput

if [ "$OSTYPE" == "linux-gnu" ]
then
    ./tests/test_basics.sh "$cwddir" "$builddir/beaknm"
    if [ "$?" != "0" ]; then echo ERRRROROROR in basic tests; exit 1; fi
fi

echo
echo

for i in $testscripts
do
    thetest=$(basename $i)
    thetest=${thetest%.sh}
    testdir=$testoutput/$thetest
    echo $thetest
    if [ "$thetest" != "test_basics" ]
    then
        $i $beak $testdir
        if [ $? == "0" ]; then echo OK; fi
    fi
done
