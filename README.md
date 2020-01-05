## The Beak backup tool

Beak is a backup-mirror-share-rotation-pruning tool that is designed
to co-exist with the cloud/remote storage (rclone, rsync) of your
choice. Beak enables push/pull-merge to share the remotely
stored backups between multiple client computers.

Note! The storage format is still changing!

| OS           | Status           |
| ------------ |:-------------:|
|GNU/Linux & MacOSX| [![Build Status](https://travis-ci.org/weetmuts/beak.svg?branch=master)](https://travis-ci.org/weetmuts/beak) |

## Summary

beak is the tool for the impatient who wants to initiate a backup
manually and want to see it finished, quickly. In other words, after
finishing some important work, you trigger the backup and wait for it
to finish, which should not take too long. This is necessary for me,
since I often work on my laptop. I want my work to be backed up before
I close the laptop and put it in my bag. Once in the bag, the
laptop risks being dropped on the ground and destroyed or risks being stolen
or it might break down because the warranty just expired.

I also often move my work between the laptop and a stationary computer
and it is convenient to push/pull-merge the work between them since
the work might not (yet) be stored in remote git repository nor in a
distributed file system.

I also like to have multiple backups in multiple locations, in the latop,
on a removable storage, on multiple remote cloud storages etc. The backups can be
pruned with different rules for different storage locations.

I also like to keep large data sets in the cloud and not on the latop,
then I simply mount the remote cloud to access it. To have a proper
backup of the large data, it is of course imperative to have it stored
in multiple independent cloud locations.

## Short short manual

Local filesystem backups:
```
# Store whenever you feel the need to make a backup.
>beak store /home/you/Work /home/backups
>beak shell /home/backups
Mounted /home/backups
Exit shell to unmount backup.
>ls
2017-03-14_12:12  2018-02-05_11:08  2018-09-23_09:17
>find . -name "MyMissingFile"
>exit
Unmounting backup /home/you/backups
```

Remote cloud backups:
```
# Just replace your local backup directory with an rclone storage:
>beak store /home/you/Work s3_work_crypt:
>beak shell s3_work_crypt:
```

Restoring is just as easy.
```
# You can restore the third most recent backup. (0=latest 1=second latest 2=third latest)
>beak restore s3_work_crypt:@2 /home/you/Work
```

Add preconfigured rules for more complicated backup setups.
```
# Add a beak rule named work: for /home/you/Work
>beak config
# Depending on the configuration push might store in multiple local and remote locations.
>beak push work:
# Pull will pull the latest backup and merge it into your origin.
>beak pull work:
```

Now, you do not want to store backups forever.
```
# Prune the local backup
>beak prune /home/backups
# or the cloud backup
>beak prune s3_work_crypt:
# or the rule
>beak work:
```

## Longer manual

Beak is the tool for the impatient who wants to initiate a backup
manually and want to see it finished, quickly. In other words, after
finishing some important work, you type:

```
>beak push work:
```

and wait for it to finish.  You can now be sure that your work directory
(the _origin_) is safely backed up to the _storage location_ s3_work_crypt: in
the cloud, before you stuff your laptop in your bag.

The store created a _point in time_, which is defined to be the modify
timestamp of the most recently changed file/directory within the
_origin_ directory that was backed up. (I.e. it is not the time when
the store was initiated.)

`work:` a beak _rule_ that you have created to backup the _origin_ directory /home/you/Work to
the local backup /home/you/Work/.beak/local and then to the remote cloud storage location s3_work_crypt:

`s3_work_crypt:` is an rclone _storage location_ , for example an
encrypted directory on S3 or your Google drive, or somewhere else. You
configure such rclone locations using the command `rclone config`
([see RClone doc](https://rclone.org/docs/)).

You configure the beak _rules_ using the command `beak config`.

Another example is: `beak store work: backup@192.168.0.1:/backups`

`backup@192.168.0.1:/backups` is an rsync _storage location_. beak looks for the @ sign and the : colon, to detect that it should use rsync.

Or you can store directly to any directory in the file system: `beak store work: /media/you/USBDrive`

`/media/you/USBDrive` is just a local directory, that also happens to be a usb storage dongle.

Actually you do not need to use a beak _rule_ at all. `beak store /home/you/Work /home/backups`

But a beak _rule_ helps with remembering where the backups are stored,
how and where to store them and how to prune them.  An even simpler way of triggering the backup is therefore:

`>beak push work:`

beak will then figure out which _storage location_ to use for this
push. (It might be round robin through several _storage locations_, or always push
to all of them, or push to a special USB storage dongle if it is available.)

## Restoring a backup

You can restore the the most recent _point in time_ from a _storage location_ to a directory:

```
>mkdir RestoreHere
>beak restore s3_work_crypt: RestoreHere
```

or from an arbitrary file system backup (here we pick the fourth most recent _point in time_ @0 is the most recent, @1 the second most recent etc):

```
>beak restore /media/you/USBDrive@3 RestoreHere
```

You can also restore a backup for merge into your _origin_
directory. This will detect if you have performed modifications
to your files that are more recent than the backup you are restoring.
When it detects that a source file has been concurrently changed it will
trigger an external merge program (like meld) to finalize the changes.
For binary files, it stores the concurrently changed versions in the
directory with the suffix .CCC (concurrent change conflict).
```
>beak restore s3_work_crypt: work:
>beak restore /media/you/USBDevice /home/you/Work
```

or simply:

```
>beak pull work:
```

## Accessing files in backups

You can mount a specific _storage location_:

```
>mkdir OldStuff
>beak mount s3_work_crypt: OldStuff
```

When you mount without specifying the _point in time_ @x, then the _points in time_ will show as subdirectories.

```
>ls OldStuff
@0 2017-09-07 14:27 2 minutes ago
@1 2017-09-05 10:01 2 days ago
@2 2017-07-01 08:23 2 months ago
```

If you supply a _point in time_ (eg @2), then it will mount that particular backup.

```
>beak mount /media/you/USBDrive@2 OldStuff
```

You unmount like this:

```
>beak umount OldStuff
```

You can also do:

```
>beak shell s3_work_crypt:
```

## Accessing all backups at once.

If you have configured a _rule_ that backups in many different _storage locations_,
then the easiest way to access all your _points in time_, is to do a shell
mount.

`>beak shell work:`

```
>ls
@0 2017-09-07 14:27 2 minutes ago to s3_work_crypt:
@1 2017-09-07 14:27 2 minutes ago to local
@2 2017-09-05 10:01 2 days ago to gd_work_crypt:
@3 2017-07-01 08:23 2 months ago to s3_work_crypt:
```

## Status of your _origin_ directories

You can check the status of your configured _origin_ directories with:

`>beak status`

## Removing old backups

After a while you probably want to remove backups to save space.

`>beak prune s3_work_crypt:`

The default _keep_ configuration is:
```
All points in time for the last 2 days.
One point in time per day for the last 2 weeks.
One point in time per week for the last 2 months.
One point in time per month for the last 2 years.
```

As usual, you usually want to do:

```
>beak prune work:
```

and let the configured _rule_ select how to prune in the different _storage locations_.

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
using tar.  Tthere is a shell script that can do the proper
extraction for you, you do not need the beak binary.

Why discuss the storage format? Because the storage format _is_ the
backup system.  There are no other meta-data files needed. Try:

`>beak bmount /home/you/Work TestBeakFS`

Explore the directory `TestBeakFS` to see how beak has chunkified your
_origin_ directory. Now do:

`>rclone copy TestBeakFS /home/you/Backup`

and you have a proper backup! To unmount the chunky filesystem:

`>beak umount TestBeakFS`

Now mount and rclone again. Rclone will exit early because there is nothing to do.
Logical, since you have not changed your _origin_ directory. This is due
to the design of beak, the backup files are deterministic and has the same
timestamps and sizes.

To access the backed up data do:

`>beak mount /home/you/Backup@0 OldStuff`

Now explore OldStuff and do `diff -rq /home/you/Work OldStuff`. There should be no differences.
The @0 means to mount the most recent _point in time_ in the _storage location_.

You can of course use store, beak will either store locally or invoke rclone or rsync:

`>beak store /home/you/Work /home/you/Backup`

which will store any chunk files that are missing into Backup. The extraction process is symmetrical:

`>beak restore /home/you/Backup@0 /home/you/Work`

Cloud storages often has case-insensitive filesystems and can only store plain files.
The chunks however, will properly store your case-sensitive filesystem with symbolic links etc.
It is rare, but it might happen that you have gamesrc and GameSrc and they end up as two separate
chunked directories, that would conflict on the remote storage. Beak will detect this
and refuse to perform the backup, giving you a chance to rename the directories.

Beak will not accept files or directories containing control characters, ie chars with values 1-31,
and refuse to perform the backup, giving you a chance to rename the file/dir.

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

`>beak store /home/you/Work /home/you/Backup`

You now have two _points in time_ stored in the _storage location_. Since you made no changes
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

`>git pack s3_work_crypt:`

You can check the integrity and garbage collect any loose chunks that
are no longer referenced from index files, using:

`>beak check s3_work_crypt:`

Local backups are useful if you want to revert a mistake you yourself
did, like erasing a file or breaking some source code. If you have
btrfs or another snapshotting filesystem then beak can use snapshots for
its local backup storage to save space.

Remember, the intent of beak is to store the changes now! Quickly.
You probably have the bandwidth and the storage space. And on my 1Gbit/s internet
connection, sending a 10Mb file to a remote cloud storage takes the same
time as a minimal 10 byte file..... however odd it might seem.

The net result is that the contents (tars and index files) of a backup
are write-once. They are never updated. Beak therefore does not have
to rewrite old archive files or update meta-data. As a consequence
beak cannot destroy the previous backup if the current backup is
interrupted halfway.

If a _storage location_ (local or remote) is later packed, the new
diff chunks are first uploaded and verified to have reached their
destination, before the non-diff chunks are removed. The pack can be
done by a different computer than the computer that stored it.

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

`.beak/cache` stores downloaded chunks when you mount remote _storage locations_
or mount them all using the shell command. It is also used
to download index files when checking remote repositories, performing diffs
and restores. You can clean the cache at any time when you are not mounting
or otherwise executing a beak command.

## Command summary

```
beak store <origin> <storage>    beak restore <storage> <origin>

beak shell {<rule>|<storage>}

beak mount <storage> <dir>       beak bmount <origin> <dir>

beak fsck <storage>

beak push <rule>                 beak pull <rule>

beak diff {<storage>|<origin>|<rule>} {<storage>|<origin>|<rule>}

beak config

beak status {<rule>|<storage>}

beak prune {<rule>|<storage>}

beak pack {<rule>|<storage>}
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
* x86_64-apple-darwin18.2.0

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

### Compiling on Darwin

Install brew from https://brew.sh/

`brew install pkg-config`
`brew install openssl`

Install FUSE for macOS from https://osxfuse.github.io/

`export PKG_CONFIG_PATH="/usr/local/opt/openssl/lib/pkgconfig"`

`./configure`

`make`
