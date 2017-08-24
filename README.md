# tarredfs
Beak is a mirroring-backup-rotation-retention tool that is designed to co-exist
with the (cloud) storage of your choice and allow push/pull to share the backups
between multiple client computers.

There is no need for an active server, merely a storage location (local or remote).

See the home page for installation, usage, documentation, changelog
and configuration walkthroughs.

http://nivelleringslikaren.eu/beak/

Tarredfs, a libfuse filesystem that renders your files in virtual tar files.
by Fredrik Öhrström oehrstroem@gmail.com

If you want to rclone your data to a cloud storage, but the cloud storage provider has
a transfer limit of 1 file per second or so. Then you want to automatically concatenate
multiple small files into larger files that take at least a second or so to transfer.

This is what tarredfs does! 

The default settings:
* trigger tar file creation in a directory if its contents is at least 5 MiB.
* if possible merge as many small files as possible to create tar files of 10 MiB. 
* A file larger than 10MiB get its own tar file.
* always trigger tar file creation in the first subdirectory level.

First mount a tarredfs:
```
>tarredfs root mount
Scanning /home/you/root
Mounted /home/you/mount with 3 virtual tars with 33 entries in 379ms.
```

Then extract the tars in check and compare that root and check is identical.
```
>mkdir -p check; cd check
tarredfs-untar xv ../mount
cd ..; tarredfs-compare root check
```
(`diff -rq root check` also works but does not check the time stamps)

Or if your root is so big that you do not want to clone a copy, then you
can run an integrity test for 120 seconds, where it first performs a compare and then extracts a random file from the tars. It is compared with the original in root. If ok, then it is deleted and the process continues until the time is up.
```
>tarredfs-integrity-test root mount 120
 OK
 OK (10) foo/bar.c
 ...
 ..
 .
```

# Development
* Clone: `git clone git@github.com:jabberbeak/tarredfs.git ; cd tarredfs`
* Build: `make` Your executables are now in `build/tarredfs*`.
* Test: `make test`
* Install: `sudo make install` Installs in /usr/local/bin

# Dependencies

A copy of libtar is included because there are a few bug fixes necessary for long file names etc. 

The usual build dependencies for c++ builds on debian based systems.
