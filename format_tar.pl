while (<STDIN>) {
    if ($_ =~ m/Tarredfs: tar/) {
    } else {
        if ($_ =~ m/V---------/) {
        } else {
            if (my($bi,$ow,$si,$da,$ti,$fi) = $_ =~ m/([dlcbrwxst-]{10}) +(\S+\/\S+) +([\d,]+) +(\d\d\d\d-\d\d-\d\d) (\d\d:\d\d) (.*)/) {

                $fi =~ s/^\.\///;
                if ($fi ne ".") {
                    $fi =~ s/\\\\/\\/g;
                    if ($bi =~ m/^d.*/) {
                        # This is a directory, make sure it ends with slash
                        $fi = "$fi/";
                    }
                    print("$fi $bi $ow $si $da $ti\n");
                }
            }
            else {
                $fi = $_;
                $fi =~ s/^\s+|\s+$//g;
                if ($fi ne "") {
                    print("WOOT >$fi<");
                }
            }
        }
    }
}
