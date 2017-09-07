# beak

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
`beak push work gdrivecrypt:/work` done!

You sit down in front of the desktop computer and performs:
`beak pull work gdrivecrypt:/work` done!

You do not have to switch computer to do a `push`. Do it whenever you
have done some useful work and want to make sure it is not lost. You
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

Run `beak push work gdrivecrypt:/work` to push your work to a cloud drive.
Run `beak push work /media/myself/usbdrive/work` to push your work to a local drive.

Run `beak mount gdrivecrypt:/work` gives you access to the remote data in the virtual
directories .beak/gdrivecrypt/work

Run `beak prune work gdrivecrypt:` to prune the points in time stored in gdrivecrypt: for work.

# Development
* Clone: `git clone git@github.com:weetmuts/beak.git ; cd beak`
* Configure: `./configure`
* Build: `make` Your executables are now in `build/tarredfs*`.
* Test: `make test`
* Install: `sudo make install` Installs in /usr/local/bin

Hosts to be supported: x86_64-linux-gnu x86_64-w64-mingw32 arm-linux-gnueabihf

# Cross compiling

`./configure --host=x86_64-w64-mingw32 --with-zlib=../zlib-1.2.11/`