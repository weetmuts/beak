#!/usr/bin/env bash

builddir="$1"
test="$2"
cwddir=$(pwd)

$builddir/testinternals

beak="$builddir/beak"
testscripts=$(ls tests/test*.sh)
testoutput=$(pwd)/test_output

rm -rf $testoutput
mkdir -p $testoutput

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

./tests/test_basics.sh "$cwddir" "$builddir/beak" "$test"
if [ "$?" != "0" ]; then echo "ERROR: basics" ; exit 1; fi

for i in $testscripts
do
    thetest=$(basename $i)
    thetest=${thetest%.sh}
    testdir=$testoutput/$thetest
    if [ "$thetest" != "test_basics" ]
    then
        if [ "$test" = "" ] || [[ "$testtest" =~ "$test" ]]
        then
            echo $thetest
            $i $beak $testdir
            if [ $? == "0" ]; then echo OK; fi
        fi
    fi
done
