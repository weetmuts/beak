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

if [ "$1" == "--nodirs" ]; then
    NODIRS="-type f"
    shift
else
    NODIRS=""
fi

if [ "$1" == "" ] || [ "$2" == "" ]; then
    echo Usage: beak-compare [root] [extracted]
    echo
    echo Used to check that the contents extracted from a beak backup
    echo directory are identical to the origin.
    exit
fi


dir=$(mktemp -d /tmp/beak_compareXXXXXXXX)

function finish {
    rm -rf $dir
}
trap finish EXIT

(cd "$1" && find . $NODIRS -printf '%p\t%TY-%Tm-%Td_%TT\t%M\t%u %g\t%s\t%l\n' | sort > $dir/org.txt)
(cd "$2" && find . $NODIRS -printf '%p\t%TY-%Tm-%Td_%TT\t%M\t%u %g\t%s\t%l\n' | sort > $dir/dest.txt)

diff $dir/org.txt $dir/dest.txt
