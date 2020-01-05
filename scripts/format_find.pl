while (<STDIN>) {
    my($bi,$ow,$si,$da,$ti,$fi,$li) = $_ =~ m/([dlcbrwxt-]{10})\0(\S+\/\S+)\0(\d+)\0(\d\d\d\d-\d\d-\d\d)\0(\d\d:\d\d)\0([^\0]+)\0(.*)/;

    $fi =~ s/^\.\///;
    $t = substr($bi, 0, 1);
    if ($t ne "-") {
        $si = "0";
    }
    if ($t eq "d") {
        $fi = "$fi/";
    }
    if ($t eq "c" || $t eq "b") {
        my ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$size,
            $atime,$mtime,$ctime,$blksize,$blocks)
            = stat("$ARGV[0]/$fi");
        $major = $rdev >> 8;
        $minor = $rdev & 255;
        $si = "$major,$minor";
    }
    if ($li ne "") {
        $fi = "$fi -> $li";
    }
    if ($fi ne "./") {
        print("/$fi $bi $ow $si $da $ti\n");
    }
}
