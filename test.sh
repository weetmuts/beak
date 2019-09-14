#!/bin/bash

builddir="$1"

echo testinternals
$builddir/testinternals

beak="$builddir/beak"
testscripts=$(ls tests/test*.sh)
testoutput=$(pwd)/test_output

rm -rf $testoutput
mkdir -p $testoutput

for i in $testscripts; do
    thetest=$(basename $i)
    thetest=${thetest%.sh}
    testdir=$testoutput/$thetest
    echo $thetest
    $i $beak $testdir
    if [ $? == "0" ]; then echo OK; fi
done

./test_basics.sh $builddir/beak
