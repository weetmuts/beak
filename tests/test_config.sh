#!/bin/bash

beak=$1
dir=$2

rm -rf $dir/origin
mkdir -p $dir/origin
rm -rf $dir/storage
mkdir -p $dir/storage

echo 12345678 > $dir/origin/hello.txt

cat > $dir/test.conf <<EOF
[test]
origin = $dir/origin
type = LocalThenRemoteBackup
cache = .beak/cache
cache_size = 1 GiB
local = .beak/local
local_keep = all:1d
remote = $dir/storage
remote_type = FileSystemStorage
remote_keep = all:1w daily:2w weekly:2m monthly:2y
EOF

$beak push --useconfig=$dir/test.conf test: > $dir/test.log
