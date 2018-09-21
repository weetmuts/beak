
main.cc:
   Bootstrap filesystems.
   Load config file and configure settings for commands.
   Invoke the proper beak function based on the command.

beak.h beak.cc:
   The beak interface, definitions of commands and options.
   Implementations of the commands.

always.h always.cc:
    Defines and templates used everywhere.

log.h log.cc:
    Log functions, debug, verbose, error, warning etc.

util.h util.cc util_posix.cc util_winapi.cc:
    Utility functions.

ui.h ui.cc:
    Input output functions for communcating with the user.

configuration.h configuration.cc:
    Load the config file.

filesystem.h filesystem.cc filesystem_posix.cc filesystem_winapi.cc filesystem_winapi.h:
    Implements a generic FileSystem api and posix/winapi implementations.
    Implements a Fuse API wrapper that takes a fuse api and exports a FileSystem api.
    Also implements a caching filesystem.

backup.h backup.cc
    Create the virtual beak filesystem from the origin fs,
    ie grouping the origin files into virtual tars.
    Exposing Fuse API access to the virtual beak file system.
    The Fuse api can either be directly mounted by fuse,
    or wrapped in a FileSystem api and handed to storagetool
    for storing the backup in a remote location.

restore.h restore.cc
    Take a virtual beak filesystem from a storage fs,
    restructuring the original files stored in the backup,
    exporting a Fuse API to access the files.
    The fuse api can either be directly mounted by fuse,
    or wrapped in a FileSystem api and handed to origintool
    for restoring the files into the origin fs.

match.h match.cc:
    Pattern matches for files and directories.

nofuse.h nofuse.cc:
    Included when building without fuse support.

origintool.h origintool.cc:
    Used to restore a FileSystem api into the actual OS filesystem.

storagetool.h storagetool.cc:
    Used to store a FileSystem api into a storage location.

storage_rclone.h storage_rclone.cc:
    Utility functions for using rclone.

fit.h fit.cc:
    Curve fitting to predict completion times when storing/restoring.


statistics.h statistics.cc:
    Count the number of files stored and time remaining.

system.h system_posix.cc system_winapi.cc:
    Utility functions to call programs in the OS and to get timed callbacks.

tar.h tar.cc:
    Code to generate tar compatible headers and checksums.

tarfile.h tarfile.cc:
    The beak file system consists of tar files (.tar) and index files (.gz)
    This code knows how to track and generate the tar files.

tarentry.h tarentry.cc:
    Every backed up file/link/etc is stored as a tar entry inside a tar fle.
    This code knows how to track and generate tar entries.

index.h index.cc:
    Implements how to load the gz index files.

diff.h diff.cc:
    Calculate differences between points in time.

testinternals.cc
    Code to test beak.