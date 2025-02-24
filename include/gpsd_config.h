/* gpsd_config.h generated by scons, do not hand-hack. */

#ifndef GPSD_CONFIG_H

#define VERSION "3.25.1~dev"
#define REVISION "3.25.1~dev-2024-10-18T01:54:33"
#define GPSD_PROTO_VERSION_MAJOR 3
#define GPSD_PROTO_VERSION_MINOR 15
#define GPSD_URL "https://gpsd.io/"

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#if !defined(_BSD_SOURCE)
#define _BSD_SOURCE
#endif

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

/* #undef HAVE_LIBUSB */

#define HAVE_LIBRT 1

/* #undef HAVE_LIBNSL */

/* #undef HAVE_LIBNSOCKET */

/* #undef HAVE_LIBTHR */

/* #undef HAVE_DBUS */

/* #undef ENABLE_BLUEZ */

#define HAVE_LINUX_CAN_H 1

#define HAVE_STDATOMIC_H 1

#define HAVE_BUILTIN_ENDIANNESS 1

/* #undef HAVE_ENDIAN_H */

/* #undef HAVE_SYS_ENDIAN_H */

#define HAVE_ARPA_INET_H 1

#define HAVE_LINUX_SERIAL_H 1

#define HAVE_NETDB_H 1

#define HAVE_NETINET_IN_H 1

#define HAVE_NETINET_IP_H 1

#define HAVE_SYS_SYSMACROS_H 1

#define HAVE_SYS_SOCKET_H 1

#define HAVE_SYS_UN_H 1

#define HAVE_SYSLOG_H 1

#define HAVE_TERMIOS_H 1

/* #undef HAVE_WINSOCK2_H */

#define STRERROR_R_STR

#define SIZEOF_TIME_T 8

#define HAVE_CFMAKERAW 1

#define HAVE_CLOCK_GETTIME 1

#define HAVE_DAEMON 1

#define HAVE_FCNTL 1

#define HAVE_GETOPT_LONG 1

#define HAVE_STRLCAT 1

#define HAVE_STRLCPY 1

/* AIVDM support */
#define AIVDM_ENABLE 1

/* alias for NMEA0183 support, deprecated */
#define ASHTECH_ENABLE 1

/* application binaries directory */
#define BINDIR "bin"

/* BlueZ support for Bluetooth devices */
/* #undef BLUEZ_ENABLE */

/* client debugging support */
#define CLIENTDEBUG_ENABLE 1

/* control socket for hotplug notifications */
#define CONTROL_SOCKET_ENABLE 1

/* build with code coveraging enabled */
/* #undef COVERAGING_ENABLE */

/* enable DBUS export support */
/* #undef DBUS_EXPORT_ENABLE */

/* add debug information to build, unoptimized */
/* #undef DEBUG_ENABLE */

/* add debug information to build, optimized */
/* #undef DEBUG_OPT_ENABLE */

/* documents directory */
#define DOCDIR "share/gpsd/doc"

/* DeLorme EarthMate Zodiac support */
#define EARTHMATE_ENABLE 1

/* EverMore binary support */
#define EVERMORE_ENABLE 1

/* Jackson Labs Fury and Firefly support */
#define FURY_ENABLE 1

/* San Jose Navigation FV-18 support */
#define FV18_ENABLE 1

/* Garmin kernel driver support */
#define GARMIN_ENABLE 1

/* Garmin Simple Text support */
#define GARMINTXT_ENABLE 1

/* Geostar Protocol support */
#define GEOSTAR_ENABLE 1

/* Furuno GPSClock support */
#define GPSCLOCK_ENABLE 1

/* gpsd itself */
#define GPSD_ENABLE 1

/* privilege revocation group */
#define GPSD_GROUP "dialout"

/* privilege revocation user */
#define GPSD_USER "nobody"

/* gspd client programs */
#define GPSDCLIENTS_ENABLE 1

/* Javad GREIS support */
#define GREIS_ENABLE 1

/* icon directory */
#define ICONDIR "share/gpsd/icons"

/* implicit linkage is supported in shared libs */
#define IMPLICIT_LINK_ENABLE 1

/* header file directory */
#define INCLUDEDIR "include"

/* Spectratime iSync LNRClok/GRCLOK support */
#define ISYNC_ENABLE 1

/* iTrax hardware support */
#define ITRAX_ENABLE 1

/* system libraries */
#define LIBDIR "lib"

/* build C++ bindings */
#define LIBGPSMM_ENABLE 1

/* special Linux PPS hack for Raspberry Pi et al */
/* #undef MAGIC_HAT_ENABLE */

/* build help in man and HTML formats.  No/Auto/Yes. */
#define MANBUILD "auto"

/* manual pages directory */
#define MANDIR "share/man"

/* maximum allowed clients */
#define MAX_CLIENTS 64

/* maximum allowed devices */
#define MAX_DEVICES 6

/* MIB directory */
#define MIBDIR "share/snmp/mibs/gpsd"

/* turn off every option not set on the command line */
/* #undef MINIMAL_ENABLE */

/* Navcom NCT support */
#define NAVCOM_ENABLE 1

/* build with ncurses */
#define NCURSES_ENABLE 1

/* NMEA2000/CAN support */
#define NMEA2000_ENABLE 1

/* don't symbol-strip binaries at link time */
/* #undef NOSTRIP_ENABLE */

/* Motorola OnCore chipset support */
#define ONCORE_ENABLE 1

/* Disciplined oscillator support */
#define OSCILLATOR_ENABLE 1

/* pkgconfig file directory */
#define PKGCONFIG "lib/pkgconfig"

/* installation directory prefix */
#define PREFIX "/usr/local"

/* build with profiling enabled */
/* #undef PROFILING_ENABLE */

/* build Python support and modules. */
#define PYTHON_ENABLE 1

/* coverage command for Python progs */
#define PYTHON_COVERAGE "coverage run"

/* Python module directory prefix */
/* #undef PYTHON_LIBDIR */

/* Python shebang */
#define PYTHON_SHEBANG "/usr/bin/env python"

/* build Qt bindings */
#define QT_ENABLE 1

/* version for versioned Qt */
/* #undef QT_VERSIONED */

/* Suffix for gpsd version */
/* #undef RELEASE */

/* Directory for run-time variable data */
#define RUNDIR "/run"

/* system binaries directory */
#define SBINDIR "sbin"

/* build shared libraries, not static */
#define SHARED_ENABLE 1

/* share directory */
#define SHAREDIR "share/gpsd"

/* export via shared memory */
#define SHM_EXPORT_ENABLE 1

/* SiRF chipset support */
#define SIRF_ENABLE 1

/* Skytraq chipset support */
#define SKYTRAQ_ENABLE 1

/* run tests with realistic (slow) delays */
/* #undef SLOW_ENABLE */

/* data export over sockets */
#define SOCKET_EXPORT_ENABLE 1

/* squelch gpsd_log/gpsd_hexdump to save cpu */
/* #undef SQUELCH_ENABLE */

/* Novatel SuperStarII chipset support */
#define SUPERSTAR2_ENABLE 1

/* system configuration directory */
#define SYSCONFDIR "etc"

/* Logical root directory for headers and libraries.
For cross-compiling, or building with multiple local toolchains.
See gcc and ld man pages for more details. */
/* #undef SYSROOT */

/* systemd socket activation */
#define SYSTEMD_ENABLE 1

/* Prefix to the binary tools to use (gcc, ld, etc.)
For cross-compiling, or building with multiple local toolchains.
 */
/* #undef TARGET */

/* target platform for cross-compiling (linux, darwin, etc.) */
#define TARGET_PLATFORM "linux"

/* target Python version as command */
#define TARGET_PYTHON "python"

/* time-service configuration */
/* #undef TIMESERVICE_ENABLE */

/* True North Technologies support */
#define TNT_ENABLE 1

/* DeLorme TripMate support */
#define TRIPMATE_ENABLE 1

/* Trimble TSIP support */
#define TSIP_ENABLE 1

/* udev rules directory */
#define UDEVDIR "/lib/udev"

/* Directory for systemd unit files */
#define UNITDIR "/lib/systemd/system"

/* libusb support for USB devices */
#define USB_ENABLE 1

/* include xgps and xgpsspeed. */
#define XGPS_ENABLE 1


#define GPSD_CONFIG_H
#endif /* GPSD_CONFIG_H */
