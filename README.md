Beak is a backup-mirror-share-rotation-pruning tool that is designed
to co-exist with the storage (cloud and otherwise) of your
choice. Beak enables push/pull to share the remotely stored backups
between multiple client computers.

Beak is work in progress. The text below describes the goal!

## Manual push

beak is the tool for the impatient who wants to initiate a backup
manually and want to see it finished, quickly. In other words, after
finishing some important work, you type `beak push work: gd_work_crypt:`
and wait for it to finish.  You can now be sure that your work directory
(the _origin_) is safely backed up to the _backup_ location gd_work_crypt: in
the cloud, before you stuff your laptop in your bag.

The push created a _point in time_, which is defined to be the modify
timestamp of the most recently changed file/directory within the
_origin_ directory that was backed up. (I.e. it is not the time when
the push was initiated.)

`work:` a beak _rule_ that you have created to backup the _origin_ directory /home/you/Work.

You configure the beak _rules_ using the command `beak config`.

`gd_work_crypt:` is an rclone _backup_ location , for example an encrypted directory in your google drive.

Another example is: `beak push work: backup@192.168.0.1:/backups`

`backup@192.168.0.1:/backups` is an rsync _backup_ location. beak looks for the @ sign to detect that it should use rsync, otherwise it is considered to be an rclone location.

Or you can push directly to the file system: `beak push work: /media/you/USBDrive`

`/media/you/USBDrive` is just a local directory, that also happens to be a usb storage dongle.

Actually you do not need to use a beak _rule_ at all. `beak store /home/you/Work /home/backups`

But a beak _rule_ helps with remembering where the backups are stored, how to push to them and how to prune them.
The _rule_ can also store local backups, in addition to remote backups. Thus normally you would just type:

`beak push work:`

beak will then figure out which _backup_ location to use for this push. (It might be round robing through several locations, or all of them always, or push to a special USB storage dongle if it is available.)

The easiest way to access your _points in time_, is to do a history
mount. Create the directory OldStuff, then do:

`beak history work: OldStuff`

(The history command mounts all known local and remote _backup_ locations and merges
their history into single timeline.)

Now you can simply browse OldStuff and copy whatever file your are
missing from the historical backups. You will see the _points in
times_ as subdirectories, marked with the _backup_ location
it was pushed to. You can have the same backup in multiple _backup_ locations.

```
>ls OldStuff
@0 2017-09-07 14:27 2 minutes ago to gd_work_crypt:
@1 2017-09-07 14:27 2 minutes ago to local
@2 2017-09-05 10:01 2 days ago to gd_work_crypt:
@3 2017-07-01 08:23 2 months ago to s3_work_crypt:
```

You can check the status of your configured _origin_ directories with:

`beak status`

After a while you probably want to remove backups to save space.

`beak prune gd_work_crypt:`

The default _keep_ configuration is:
```
All points in time for the last 2 days.
One point in time per day for the last 2 weeks.
One point in time per week for the last 2 months.
One point in time per month for the last 2 years.
```

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
/home/you/Work/rclonesrc/a lot of files and subdirectories
/home/you/Work/gamesrc/a lot of files and subdirectories
/home/you/Work/only the two subdirectories above.
becomes
/home/you/Work/rclonesrc/s01[...]tar y01.tar z01[...]gz
/home/you/Work/gamesrc/s01[...]tar y01[...]tar z01[...]gz
/home/you/Work/y01[...]tar z01[...]gz
```

The order and selection of the chunky directories is deterministic (and depth first),
which means that as long as you do not modify the contents of the
_origin_ directory, then the created virtual chunky file system will
be the same.

The index provides for quick access to the contents of the chunks.  As
a precaution, the chunks also happen to be valid GNU tar files.  If
the index file is lost, then it is still possible to extract the data
using tar.  In fact, there is a shell script that can do the proper
extraction for you.

Why discuss the storage format? Because the storage format _is_ the
backup system.  There are no other meta-data files needed. This means
that you can roll your own backups without any configuration if you
wish.

`beak mount /home/you/Work TestBeakFS`

Explore the directory `TestBeakFS` to see how beak has chunkified your
_origin_ directory. Now do:

`rclone copy TestBeakFS /home/you/Backup`

and you have a proper backup! To unmount the chunky filesystem:

`beak umount TestBeakFS`

Now mount and rclone again. Rclone will exit early because there is nothing to do.
Logical, since you have not changed your _origin_ directory.

To access the backed up data do:

`beak remount /home/you/Backup@0 OldStuff`

Now explore OldStuff and do `diff -rq /home/you/Work OldStuff`. There should be no differences.
The @0 means to mount the most recent _point in time_ in the _backup_ location.

You can skip the rclone step if the _backup_ location is a local directory. Then simply do:

`beak store /home/you/Work /home/you/Backup`

which will store any chunk files that are missing into Backup. The extraction process is symmetrical:

`beak restore /home/you/Backup@0 /home/you/Work`

(This will not delete superfluous files. Add --delete-superfluous to remove them automatically.)

Cloud storages often has case-insensitive filesystems and can only store plain files.
The chunks however, will properly store your case-sensitive filesystem with symbolic links etc.
It is rare, but it might happen that you have gamesrc and GameSrc and they end up as two separate
chunked directories, that would conflict on the remote storage. Beak will detect this
and refuse to perform the backup, giving you a chance to rename the directories.

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

(Those y01[...]tar files stores directories and hard links and are only used
when extracting the backup using the shell script.)

Assuming that your _origin_ directory `/home/you/Work/` above contained `rclonesrc/` and `gamesrc/`
and you made the backup to `/home/you/Backup`. Now modify a file in `gamesrc/` and do another backup:

`beak store /home/you/Work /home/you/Backup`

You now have two _points in time_ stored in the _backup_ location. Since you made no changes
to `rclonesrc` the new index file will point to the existing rclonesrc/chunks.

![Beak FS](./doc/beak_fs.png)

If you know how git stores directory structures or how btrfs stores files or how
clojure deals with data structures, or any other system that reuse
old nodes in new trees, then you will feel right at home. :smiley:

## Efficiency

Of course if the chunk file is 10MiB and we changed a single byte in
a small file, then there is a lot of unnecessarily backed up data. But
if we store local backups, then beak can create diff chunks and diff chunks can
even be created inside an existing backup using:

`git pack s3_work_crypt:`

You can get information on a _backup_ location using:

`beak info s3_work_crypt:`

You can check the integrity and garbage collect any loose chunks that
are no longer referenced from index files, using:

`beak check s3_work_crypt:`

Local backups are useful if you want to revert a mistake you yourself
did, like erasing a file or breaking some source code. If you have
btrfs or another snapshotting filesystem then beak can use snapshots for
its local backup storage to save space.

Remember, the intent of beak is to push the changes now! Quickly.
You probably have the bandwidth and the storage space. And on my 1Gbit/s internet
connection, sending a 10Mb file to a remote cloud storage takes the same
time as a minimal 10 byte file..... however odd it might seem.

The net result is that the contents (tars and index files) of a backup
are write-once. They are never updated. Beak therefore does not have
to rewrite old archive files or update meta-data. As a consequence
beak cannot destroy the previous backup if the current backup is
interrupted halfway.

If a _backup_ location (local or remote) is later packed, the new
diff chunks are first uploaded and verified to have reached their
destination, before the non-diff chunks are removed. The pack can be
done by a different computer than the computer that pushed.

## Use cases

As you might have guessed, beak is not the best tool to restore your whole
hard disk image from scratch after a hard disk failure. If you are re-installing
on an empty disk you can use `beak checkout s3_work_crypt: work:` after
re-configuring rclone and beak. You can store your home directory in beak.

beak is more like a Swiss army knife for backing up, mirroring,
off-site storage etc. As you have seen, you can do everything by hand
using: `beak mount` `beak store` and `rclone`.  But to speed up common tasks you
can configure rules with `beak config`. A _rule_ (like work:) provides a short cut to the
actual _origin_ directory (like /home/you/Work). The push and pull commands require a _rule_.

A configured _rule_ can be of a type:

```
LocalAndRemoteBackups = A push stores locally and the rclones to the remote. Diff chunks are possible.
RemoteBackupsOnly     = A push rclones directly to the remote from a virtual BeakFS.
MountOnly             = Only mount off-site storage
```

A _source directory_ can have multiple remotes pre-configured and push round-robin
or to all of them at every push.

You can have separate configurations your Work, your Media, your
Archive, your Home directory etc.  If you use S3 you can have separate
passwords for the remote storage locations to create safety barriers.

You can have different _keep_ configurations for all _backup_ locations. Your local backup
can be kept at a minimum and if you are using btrfs it can use snapshots.

You can move your work between computers using beak. A typical use case for me is:
happily programming on your laptop and the suddenly, the kids must see frozen
and you have to relinguish the laptop before child-induced calamity ensues.
beak comes to the rescue:

`beak push work: gd_work_crypt:`

You then sit down in front of the desktop computer and perform:

`beak pull work: gd_work_crypt:`

Pull will fetch remote chunk files and then update your _source directory_
accordingly and try to merge the contents if possible. This can be done
in separate steps using `beak fetch` and `beak merge`.

You can examine the differences between your current _source directory_
and a backup, or the difference between two backups using:

`beak diff work: s3_work_crypt@0`

`beak diff s3_work_crypt@0 s3_work_crypt@1`

If you do not have a user file system (FUSE) available you can use:

`beak shell s3_work_crypt:`

To start a minimal shell to simply find and copy data from the backup.


## Configuration options

A standard setup of the _source directory_ `work:` above, would place
a beak directory here: `/home/you/Work/.beak`. Beak avoids
backing up any files inside a .beak directory.

Within `.beak` there is:

`.beak/local` where the local backup is stored before it is pushed to
the remote.  The local storage is also used to create diff chunks. If
using a btrfs filesystem then the btrfs snapshots are stored here as well.
Beak will use a snapshot and recreate the old _beakfs_ from that snapshot
instead of storing the _beakfs_ files. Thus saving disk space.

`.beak/cache` stores downloaded chunks when you mount remote _backup_ locations
or mount them all using the history command. You can clean the cache
at any time when you are not mounting or otherwise executing a beak command.

`.beak/history` is the default location to place the _point in time_ history,
if `beak history work:` is invoked without a target directory.

## Command summary

```
beak store     beak restore

beak mount     beak remount

beak push      beak pull      beak history

beak fetch     beak merge     beak diff

beak shell

beak config

beak status    beak info      beak check

beak prune     beak pack
```

## Development

* Clone: `git clone git@github.com:weetmuts/beak.git ; cd beak`
* Configure: `./configure`
* Build: `make` Your executable is now in `build/x86_64-pc-linux-gnu/release/beak`.
* Build: `make debug` Your executable is now in `build/x86_64-pc-linux-gnu/debug/beak`.
* Test:  `make test` or `./test.sh binary_to_test`
* Install: `sudo make install` Installs in /usr/local/bin

Hosts supported:
* x86_64-pc-linux-gnu
* x86_64-w64-mingw32
* arm-linux-gnueabihf

To have beak print detailed debug information do: `export BEAK_DEBUG_hardlinks=true`

You can also for example do `--log=forward,hardlinks` on the command line. Adding `--log=all`
will log all debug information.

Use the option `--listlog` to print all possible debug parts.

## Cross compiling to Winapi and Arm.

You can have multiple configurations enabled at the same time.

* `make` builds release for all configured hosts.
* `make debug` builds debug for all configured hosts.
* `make debug linux64` builds only debug for gnu/linux x86_64 hosts.
* `make debug winapi64` builds only debug for winapi 64 bit hosts.
* `make debug arm32` builds only debug for gnu/linux arm 32 bit hosts.

`./configure`

`./configure --host=x86_64-w64-mingw32 --with-zlib=3rdparty/zlib-1.2.11-winapi --with-openssl=3rdparty/openssl-1.0.2-winapi`

`./configure --host=arm-linux-gnueabihf --with-fuse=3rdparty/libfuse-arm/usr --with-openssl=3rdparty/openssl-1.0.2-arm --with-zlib=3rdparty/zlib-1.2.11-arm`