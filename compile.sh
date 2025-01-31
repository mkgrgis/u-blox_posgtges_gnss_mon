d=$(dirname $0);
gcc -o $d/gpsmon.o -c -pthread -Wall -Wextra -fexcess-precision=standard -Wcast-align -Wcast-qual -Wimplicit-fallthrough -Wmissing-declarations -Wmissing-prototypes -Wno-missing-field-initializers -Wno-uninitialized -Wpointer-arith -Wreturn-type -Wstrict-prototypes -Wundef -Wvla -O2 -pthread -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600  $d/gpsmon.c

gcc -o $d/monitor_ubx.o -c -pthread -Wall -Wextra -fexcess-precision=standard -Wcast-align -Wcast-qual -Wimplicit-fallthrough -Wmissing-declarations -Wmissing-prototypes -Wno-missing-field-initializers -Wno-uninitialized -Wpointer-arith -Wreturn-type -Wstrict-prototypes -Wundef -Wvla -O2 -pthread -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I/usr/include/postgresql -lgps $d/monitor_ubx.c
gcc -o $d/pgubxgpsmon -pthread $d/gpsmon.o $d/monitor_ubx.o $d/lib/libgpsd.a $d/lib/libgps_static.a -lm -lrt -lncurses -ltinfo -lpq 



#/usr/bin/asciidoctor -b manpage -v -a gpsdweb=https://gpsd.io/ -a gpsdver=3.25.1~dev -o gpsd-3.25.1~dev/man/gpsmon.1 gpsd-3.25.1~dev/man/gpsmon.adoc
#/usr/bin/asciidoctor -b html5 -v -a gpsdweb=https://gpsd.io/ -a gpsdver=3.25.1~dev -a docinfo=shared -a docinfodir=../www/ -o gpsd-3.25.1~dev/www/gpsmon.html gpsd-3.25.1~dev/man/gpsmon.adoc
