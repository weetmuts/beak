#!/bin/bash
cat $1 | grep statistics | cut -f 2- > /tmp/beak_plot_store.dat
cat > /tmp/beak_plot_store.gnuplot <<EOF
plot '/tmp/beak_plot_store.dat' using 2:1 with lines title "measurements", \
     '/tmp/beak_plot_store.dat' using 2:3 with lines title "1s speed", \
     '/tmp/beak_plot_store.dat' using 2:4 with lines title "immediate", \
     '/tmp/beak_plot_store.dat' using 2:5 with lines title "average"

EOF

gnuplot --persist /tmp/beak_plot_store.gnuplot
