Beak is a mirror-share-backup-rotation-retention tool that is designed
to co-exist with the storage (cloud and otherwise) of your
choice. Beak enables push/pull to share the remotely stored backups
between multiple client computers.

## Manual push

beak is the tool for the impatient who wants to initiate a backup
manually and want to see it finished, quickly. In other words, after
finishing some important work, you type `beak push work: gd_work_crypt:`
and wait for it to finish.  You can now be sure that your work directory
is safely backed up to the _remote_ cloud storage gd_work_crypt: before
you stuff your laptop in your bag.

The push created a _point in time_, which is defined to be the modify
timestamp of the most recently changed file/directory within the backup.
(I.e. it is not the time when the push was initiated.)

`work: is a beak configuration to backup one of your directories, for example: /home/fredrik/Work`

(the to be backed up directory is also known as a _source directory_)

`gd_work_crypt: is an rclone destination, for example an encrypted directory in your google drive.`

(the destination can also be a local directory, both the local and the remote are known as a _backup directory_)

The easiest way to access your _points in time_, is to do a history
mount. Create the directory OldStuff, then do:

`beak history work: OldStuff`

(The history command mounts all known local and remote _backup directories_ and merges
their history into single timeline.)

Now you can simply browse OldStuff and copy whatever file
your are missing from the historical backups. You will see the _points
in times_ as subdirectories, marked with the _remote_ or _local_ storage it was pushed
to. You can have the same backup in multiple _backup directories_.

```
>ls OldStuff
@0 2017-09-07 14:27 2 minutes ago to gd_work_crypt:
@1 2017-09-07 14:27 2 minutes ago to local
@2 2017-09-05 10:01 2 days ago to gd_work_crypt:
@3 2017-07-01 08:23 2 months ago to s3_work_crypt:
```

After a while you probably want to remove unnecessary backups.

`beak prune gd_work_crypt:`

The default rule is to keep:
```
All points in time for the last 7 days.
One point in time per day for the last 14 days.
One point in time per week for the last 8 weeks.
One point in time per month for the last 12 months.
```

By default the point in time chosen for an interval, is the last point
in time for that interval.  I.e. the last recorded point in time for
that day, week or month.

## BeakFS, the chunky file system

Cloud storages often have a limit to the number of files that can be
transferred per second.  It can be as low as 2-3 files per second! If
you transfer large files, images/videos etc, then this is not a
problem. But it makes it impossible to transfer your Eclipse workspace
or other source code repositories, with hundreds or thousands of very
small files. To get around this problem, beak began as an experiment
in creating a virtual fuse filesystem that automatically makes your
filesystem chunky!

Beak finds directories with a content size (files and subdirectories)
that are at least 10MiB. It will then replace the content with
one or more virtual tar files and an index file. The path to the now
chunky directory remains the same.

```
For example:
/sources/myproject/a lot of files and subdirectories
becomes
/sources/myproject/s01.tar z01.gz
```

The order and selection of the chunky directories is deterministic (and depth first),
which means that as long as you do not modify the contents of the
_source directory_, then the created virtual chunky file system will
be the same.

The index provides for quick access to the contents of the chunks.  As
a precaution, the chunks also happen to be valid GNU tar files.  If
the index file is lost, then it is still possible to extract the data
using tar.  In fact, there is a shell script that can do the proper
extraction for you.

Why discuss the storage format? Because the storage format _is_ the
backup system.  There are not other meta-data files needed. This means
that you can roll your own backups without any configuration if you
wish.

`beak mount /home/you/Work TestBeakFS`

Explore the directory `TestBeakFS` to see how beak has chunkified your
_source directory_. Now do `rclone copy TestBeakFS /home/you/Backup`
and you have a proper backup!

`beak umount TestBeakFS`

Now remount again and rclone. Rclone will exit early because there is nothing to do.

To access the backed up data do:

`beak mount /home/you/Backup OldStuff`

Now explore OldStuff and do `diff -rq /home/you/Work OldStuff`. There should be no differences.

(You can skip the rclone step if you are storing in a local _backup directory_. Then simply do:
`beak store /home/you/Work Backup` which will store any chunk files that are missing into Backup.)

## Naming the chunky files

As you now have seen, the chunk files have long names. Why is that?

Each chunk has a unique name that consists of the most recent modify timestamp
of all the entries inside the tar, the size of the contents inside the archive
file and the hash of the meta-data of the files inside the archive file. Like this:

`s01_001501080787.579054757_1119232_8e919febd204393073e02a525270b79abdbfa7e4ba3911e28ae62e9298e044a1_0.tar`

`s01_--seconds---.--nanos--_-size--_----------metadata-hash-----------------------------------------_0.tar`

The index file, similarily named, but uses the most recently modify timestamp
of any of the archive files it indexes _and_ the modify timestamp of the containing directory.
The hash is the hash of the all the archives pointed to by this index file.

`z01_001504803295.504732149_0_cb56cc0ee219e54f7afceff6aae29843bc4a4bfa25c421b24cc5d918f524a6ff_0.gz`

`z01_--seconds---.--nanos--_0_----------metadata-hash-----------------------------------------_0.gz`

Assuming that your _source directory_ `/home/you/work/` above contained `rclonesrc/` and `gamesrc/`
and you made the backup to `/home/you/Backup`. Now modify a file in `gamesrc/` and do another backup:

`beak store /home/you/work /home/you/Backup`

You now have two points in time store in the Backup directory. Since you made no changes
to the `rclonesrc` the new index file will point to the existing rclonesrc/chunks.

![Beak FS](./doc/beak_fs.png)

If you know how git stores files or how btrfs stores files or how
clojure deals with data structures, or any other old system that reuse
old nodes in new trees, then you will feel right at home.

As you can see, all files are write only! New backups only write new files.

# Efficiency

Of course if the chunk file is 10MiB and we changed a single byte in
a small file, then there is a lot of unnecessary backed up data. But
if we store local backups, then beak can create diff chunks and diff chunks can
even be created inside an existing backup. (Similar to how git packs its files.)

Remember, the intent of beak is to push the changes now! Quickly.
You probably have the bandwidth and the storage space. And on my 1Gbit/s internet
connection, sending a 10Mb file to a remote cloud storage takes the same
time as a minimal 10 byte file..... however odd it might seem.

The net result is that the contents (tars and index files) of a backup
are write-once. They are never updated. Beak therefore does not have
to rewrite old archive files or update meta-data. As a consequence
beak cannot destroy the previous backup if the current backup is
interrupted halfway.

## Use cases

A typical use case for me: happily programming on your laptop and the
suddenly, the kids must see frozen and you have to relinguish the
laptop before child-induced calamity ensues. beak comes to the rescue:
`beak push work: gd_work_crypt:` done!

You then sit down in front of the desktop computer and perform:
`beak pull work: gd_work_crypt:` done!


You do not have to switch computer to do a `push`. Do it whenever you
have done some useful work: and want to make sure it is not lost. You
can push to any rclone remote storage locations, like:
`gd_work_crypt:` `s3_work_crypt:` or you can push to a local usb
drive: `/media/myself/usbdevice/work`.

Each push registers as a point in time (assuming data actually has
changed).  The data accumulates in the storage location. Once per week
or per month, you might want to do `beak prune gd_work_crypt:` to prune
older points in time and keep typically one per day for the last week,
one per week for the last month and one per month for the last year.

Now, you can configure push rules that rotate between different remotes
or triggers automatically when you insert a specific USB drive.
With those rules in place you can also schedule a `beak backup work:`
or `beak backup -all` to let beak chose the push destination (or destinations).

## Summary

Commands: `beak push` `beak pull` `beak checkout` `beak mount`

Low level commands: `beak view` `beak store` `beak merge` `beak fetch` `beak prune`

# Best way to understand beak is to understand its storage format

`beak mount` and `beak store` are the low level plumbing tools.
All the other features build on top of these. You can use
these tools directly if you like.

Try this command: `mkdir Test; beak mount /home/you/Work Test`

Now browse the Test directory. You will see that beak has created virtual
tar files, beginning with s01_ and y_01. You can view the contents of the
tar files using for example the `less` command. However, the full information
of where the files are stored is inside the index files, those named z01_....gz.
You can view those too using `less`.

Now do: `mkdir Again; beak mount Test BackAgain`

When you browse BackAgain, you will see your original files. The mount command
will detect if you are mounting a backup or the original filesystem.

To remove: `beak umount BackAgain`  `beak umount Test` (or use `fusermount -u BackAgain`)

You noticed that mounting the backup was quick. This is possible because it only
scans the metadata of all the files. Beak trusts your timestamps. The s01, y01 and z01 files
are virtual, when you read the s01 tar files, beak will relay the read to the original file.

Take some other directory that is not too big:

You can access any point in time not yet pruned away by mounting the storage location.
For example: `beak mount gd_work_crypt: OldWork`

When you do: `ls OldWork` you will see directories named as the points in time stored at that location:

```
@0 2017-09-07 14:27 2 minutes ago
@1 2017-09-07 14:25 3 minutes ago
@2 2017-09-05 10:01 2 days ago
@3 2017-07-01 08:23 2 months ago
```

Simply cd enter the point in time you are interested in a copy out the old data that you need.
You can also mount the latest version directly using: `beak mount gd_work_crypt:@0 LatestPush`

To get a status report of where and when you pushed, do: `beak status`

##Rclone

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

Run `beak push work: gd_work_crypt:` to push your work to a cloud drive.
Run `beak push work: /media/myself/usbdrive/work` to push your work to a local drive.

Run `beak mount gd_work_crypt:` gives you access to the remote data in the virtual
directories .beak/gdrivecrypt/work

Run `beak prune gd_work_crypt:` to prune the points in time stored in gdrivecrypt: for work.

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

`./configure --host=x86_64-w64-mingw32 --with-zlib=3rdparty/zlib-1.2.11/ --with-openssl=3rdparty/openssl-1.0.2l`

# Beak Internals


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

Thus tarredfs makes the original filesystem more chunky, ie reduces
the number of files and directories, while keeping the remaining directory structure
intact.  This is beneficial when syncing data to a
remote storage service that have high a transfer bandwidth but allows
few files to be transferred per second. Tarredfs is also a work around
for other remote storage service limitations, like case-insensitive
and lack of symbolic links.
