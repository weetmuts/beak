# beak

Beak is in development.... the text below is the target! Don't use this for real, yet! :-)

Beak is a mirroring-backup-share-rotation-retention tool that is designed to co-exist
with the (cloud) storage of your choice and allow push/pull to share the backups
between multiple client computers.

Beak is the tool for impatient people who want to initiate the backup
manually and you want to see it finish, quickly! Then you switch to
another client to continue working there. But while you are at it, it
is nice to have version control and tracking of where data is stored
in the cloud and locally.

A typical use case for me: Happily programming on your laptop and the
suddenly, the kids MUST see Frozen and you have to relinguish the
laptop before child-induced calamity ensues. Beak comes to the rescue:
`beak push work: gdrivecrypt:/work` done!

You sit down in front of the desktop computer and performs:
`beak pull work: gdrivecrypt:/work` done!

You do not have to switch computer to do a `push`. Do it whenever you
have done some useful work: and want to make sure it is not lost. You
can push to multiple storage locations, like: `gdrivecrypt:/work` `s3crypt:/work`
`media/myself/usbdevice/work`.

Each push registers as a point in time (assuming data actually has
changed).  The data accumulates in the storage location. Once per week
or per month, you might want to do `beak prune gdrivecrypt:/work` to prune
older points in time and keep typically one per day for the last week,
one per week for the last month and one per month for the last year.

You can access any point in time not yet pruned away by mounting the storage location.
For example: `beak mount gdrivecrypt:/work OldWork`

When you do: `ls OldWork` you will see directories named as the points in time stored at that location:

```
@0 2017-09-07 14:27 2 minutes ago
@1 2017-09-07 14:25 3 minutes ago
@2 2017-09-05 10:01 2 days ago
@3 2017-07-01 08:23 2 months ago
```

Simply cd enter the point in time you are interested in a copy out the old data that you need.
You can also mount the latest version directly using: `beak mount gdrivecrypt:/work@0 LatestPush`

To get a status report of where and when you pushed, do: `beak status`

# Rclone

Beak uses rclone to copy the data to the storage locations. Thus
gdrivecrypt: is an rclone target that you configure with `rclone
config`. Beak takes care of wrapping several small files into larger
archive files, this is neccessary since cloud storage locations often
have a transfer limit of two files per second. Source code, with many small
files, can therefore take a ridiculous time to rclone to the cloud!

Also beak takes care of storing symlinks and other unix:like stuff, which cannot be stored
as such in cloud providers buckets.

# Example Commands

Run `beak config` to setup directories that you want to mirror-backup-share.

Run `beak push work: gdrivecrypt:/work` to push your work to a cloud drive.
Run `beak push work: /media/myself/usbdrive/work` to push your work to a local drive.

Run `beak mount gdrivecrypt:/work` gives you access to the remote data in the virtual
directories .beak/gdrivecrypt/work

Run `beak prune gdrivecrypt:/work` to prune the points in time stored in gdrivecrypt: for work.

```
keep = all for 7-14d days then oneperday for 2w
       then oneperweek for 4w
       then onepermonth for 12m
```

week | month | mon tue wed thu fri sat sun
```

# Development
* Clone: `git clone git@github.com:weetmuts/beak.git ; cd beak`
* Configure: `./configure`
* Build: `make` Your executables are now in `build/tarredfs*`.
* Test: `make test`
* Install: `sudo make install` Installs in /usr/local/bin

Hosts to be supported: x86_64-linux-gnu x86_64-w64-mingw32 arm-linux-gnueabihf

# Cross compiling

`./configure --host=x86_64-w64-mingw32 --with-zlib=../zlib-1.2.11/`

# Beak Internals

Beak is based on a simple idea: Group the files to be backed up (the source tree),
into larger archive files and give each archive file a modify timestamp that equals
the most recently modified file inside the archive file. Now, also give the archive
file a name, that consists of that modify timestamp, the size of the contents inside the
archive file and a hash of the meta-data of the files inside the archive file. 
(The meta-data hashed is the file name (and path), its size and its modify timestamp.)
Like this:

`s01_001501080787.579054757_1119232_8e919febd204393073e02a525270b79abdbfa7e4ba3911e28ae62e9298e044a1_0.tar`

`s01_--seconds---.--nanos--_-size--_----------metadata-hash-----------------------------------------_0.tar`

Then create an index file, again with a modify timestamp that is equal
to the most recent file found in any of the archive files and the modify timestamp
of the containing directory. Name the index file
using that timestamp and a hash of the all the archives pointed to by this index file.

`z01_001504803295.504732149_0_cb56cc0ee219e54f7afceff6aae29843bc4a4bfa25c421b24cc5d918f524a6ff_0.gz`

`z01_--seconds---.--nanos--_0_----------metadata-hash-----------------------------------------_0.gz`

You now have an archive tree, rooted in a point of time, i.e. the most recent modify timestamp
of any of the archived files or the containing directory.

The grouping algorithm is not random, so this file system is predetermined from the source tree.
If the source tree has not changed, a newly mounted archive tree will be identical, to the one before.

This makes the archive tree suitable for copying, using for example rclone. RClone will check
the archive files timestamps, names and sizes. If they match what is on the remote location,
no copying takes place.

If the source tree is change, for example a file is modified, then the
most recent timestamp will be visible in the archive name containing
the modified file, and the index file name will also change due to the
new timestamp. Rclone the archive tree, will skip all the archives
that have not changed and push the new archive file and the new index
file. The index file will point to already existing (and unmodified
archives) as well as to the new archive.

The index file will not clash with the old index file, thus we have two points in time stored
at the remote location! Of course if the archive file is 10MiB and the changed file is 1KiB, there
is a lot of wasted space, but we can prune the remote storage later and we can pack the archive files later
(by replacing the absolute data with differential data). (This is very much inspired by git.)
In effect the archive files are write-once. They are never updated.

Thus, the points in time and the archive format does not require any complicated book-keeping.
It is a result of the naming conventions. Since it does not have to rewrite old archive files,
it cannot destroy the previous backup if interrupted halfway. The index file is pushed to the
remote storage, only after all the archive files have been pushed.

Packing the archive is more complicated. However it first creates the differential files, then
verifies that the difference files are properly in place, before removing the absolute files.

(It would be nice to hash on the content of the files, but that currently conflicts with the speed requirement.)

**Tell him the good part Randolph!** The good part, is that the
archive tree can be completely virtual, it does not take up any disk
space, and it can be generated quickly (since it only scans the
meta-data of the source tree).

What beak does is: whip up the virtual archive tree from your source tree, then rclone it to the other
side. As always, rclone skips any archive files already copied.

# Even more internals

For fun, if you want to study the archive tree, you can mount any directory (does not have to be
configured) like this:

`beak mount src_tree_dir archive_tree_dir` 

You can now examine the contents of the archive_tree_dir. Copy the contents from archive_tree_dir
to another directory Backups (e.g. `rclone copy archive_tree_dir/ Backups` or simply
`cp -a archive_tree_dir/* Backups`) Then do `fusermount -u archive_tree_dir`

Change something inside src_tree_dir. Again do: `beak mount src_tree_dir archive_tree_dir`
Copy the contents to Backups. Preferrably using rclone or rsync, which will skip identical files.
Unmount `fusermount -u archive_tree_dir`

Now mount the Backups. `beak mount Backups OldStuff`
Change into OldStuff and see the points in time found and the files inside those points in time.

# Even even more internals

Tarredfs generates a tarred directory (ie it contains virtual tar files) when
the size of the directory contents (including subdirectories, excluding already
tarred subdirectories) is greater than the tar trigger size (default
20MB). The search for potentially tarred directories is performed from leaf
directories to the root. Tar files are always generated in the root
and by default also in the immediate subdirectories to the root.

All directories are stored in the tar files. Directories that are
necessary to reach the tarred directories are visible in the
tarred file system. 

Thus tarredfs makes the original file system more chunky, ie reduces
the number of files and directories, while keeping the remaining directory structure
intact.  This is beneficial when syncing data to a
remote storage service that have high a transfer bandwidth but allows
few files to be transferred per second. Tarredfs is also a work around
for other remote storage service limitations, like case-insensitive
and lack of symbolic links.

