#!/bin/bash
# As the argument to this script,
# pass a log file from beak output generated with --log=statistics

cat $1  | grep "statistics: stored" | cut -f 2- > /tmp/beak_plot_store.dat
cat > /tmp/beak_plot_store.gnuplot <<EOF
f(x) = a*x**2 + b*x + c
FIT_LIMIT = 1e-6
fit f(x) '/tmp/beak_plot_store.dat' using 1:2 via a,b,c
plot '/tmp/beak_plot_store.dat',f(x)

EOF

gnuplot --persist /tmp/beak_plot_store.gnuplot
