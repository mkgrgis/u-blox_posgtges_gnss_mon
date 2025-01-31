/*
 * The generic GPS packet monitor.
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#undef USE_QT       // this progtam does not work with QT. Pacify Codacy

#include "include/gpsd_config.h"  // must be before all includes

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_GETOPT_LONG
       #include <getopt.h>
#endif
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "include/compiler.h"         // for FALLTHROUGH
#include "include/gps.h"              // for gpsd_visibilze()
#include "include/gpsdclient.h"
#include "include/gpsd.h"
#include "include/gps_json.h"
#include "include/gpsmon.h"
#include "include/strfuncs.h"
#include "include/timespec.h"

#define BUFLEN          2048

// needed under FreeBSD
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX   255
#endif  // HOST_NAME_MAX

// external capability tables
extern struct monitor_object_t ubx_mmt;
extern const struct gps_type_t driver_nmea0183;

// These are public
struct gps_device_t session;
WINDOW *devicewin;
bool serial;

// These are private
static volatile int bailout = 0;
static struct gps_context_t context;
static bool curses_active;
static WINDOW *statwin, *cmdwin;
static WINDOW *packetwin;
static FILE *logfile;
static char *type_name = "Unknown device";
static size_t promptlen = 0;
static struct termios cooked, rare;
static struct fixsource_t source;
static char hostname[HOST_NAME_MAX];
static struct timedelta_t time_offset;

// no methods, it's all device window
extern const struct gps_type_t driver_json_passthrough;
const struct monitor_object_t json_mmt = {
    .initialize = NULL,
    .update = NULL,
    .command = NULL,
    .wrap = NULL,
    .min_y = 0, .min_x = 80,    // no need for a device window
    .driver = &driver_json_passthrough,
};

static const struct monitor_object_t *monitor_objects[] = {
    &ubx_mmt,
    &json_mmt,
    NULL,
};

static const struct monitor_object_t **active;
static const struct gps_type_t *fallback;

#define display (void)mvwprintw

// termination codes
#define TERM_SELECT_FAILED      1
#define TERM_DRIVER_SWITCH      2
#define TERM_EMPTY_READ         3
#define TERM_READ_ERROR         4
#define TERM_SIGNAL             5
#define TERM_QUIT               6
#define TERM_CURSES             7

// PPS monitoring

// FIXME: Lock what?  Why?  Where?
static inline void report_lock(void)
{
    // FIXME: gpsmon, a client, should not link to the gpsd server sources!
    gpsd_acquire_reporting_lock();
}

static inline void report_unlock(void)
{
    // FIXME: gpsmon, a client, should not link to the gpsd server sources!
    gpsd_release_reporting_lock();
}

#define PPSBAR "-------------------------------------" \
               " PPS " \
               "-------------------------------------\n"

// Dummy conditional for *display* of (possibly remote) PPS events
#define PPS_DISPLAY_ENABLE 1

/******************************************************************************
 *
 * Visualization helpers
 *
 ******************************************************************************/

/* pass through visibilized if all printable, hexdump otherwise
 *
 * From: "buf" of size "len"
 * To: "buf2" of size "len2"
 *
 */
static void cond_hexdump(char *buf2, size_t len2,
                         const char *buf, size_t len)
{
    size_t i;
    bool printable = true;
    for (i = 0; i < len; i++)
        if (!isprint((unsigned char)buf[i]) &&
            !isspace((unsigned char) buf[i])) {
            printable = false;
        }
    if (printable) {
        size_t j;
        for (i = j = 0; i < len && j < len2 - 1; i++) {
            if (isprint((unsigned char)buf[i])) {
                buf2[j++] = buf[i];
                buf2[j] = '\0';
            } else {
                if (TEXTUAL_PACKET_TYPE(session.lexer.type)) {
                    if (i == len - 1 &&
                        '\n' == buf[i]) {
                        continue;
                    }
                    if (i == len - 2 &&
                        '\r' == buf[i]) {
                        continue;
                    }
                }
                (void)snprintf(&buf2[j], len2 - strnlen(buf2, len2), "\\x%02x",
                               (unsigned int)(buf[i] & 0xff));
                j = strnlen(buf2, len2);
            }
        }
    } else {
        buf2[0] = '\0';
        for (i = 0; i < len; i++) {
            str_appendf(buf2, len2, "%02x", (unsigned int)(buf[i] & 0xff));
        }
    }
}

void toff_update(WINDOW *win, int y, int x)
{
    if (NULL == win) {
        return;
    }
    if (time_offset.real.tv_sec != 0) {
        // NOTE: can not use double here due to precision requirements
        struct timespec timedelta;
        int i, ymax, xmax;

        getmaxyx(win, ymax, xmax);
        if (0 > ymax) {
            ymax = 0;  // squash a compiler warning
        }
        (void)wmove(win, y, x);
        /*
         * The magic number 18 shortening the field works because
         * we know we'll never see more than 5 digits of seconds
         * rather than 10 (because we don't print values of
         * 86400 seconds or greater in numerical form).
         */
        for (i = 0; i < 18 && x + i < xmax - 1; i++) {
            (void)waddch(win, ' ');
        }
        TS_SUB(&timedelta, &time_offset.clock, &time_offset.real);
        // (long long) for 32-bit CPU with 64-bit time_t
        if (86400 < llabs(timedelta.tv_sec)) {
            // more than one day off, overflow
            // need a bigger field to show it
            (void)mvwaddstr(win, y, x, "> 1 day");
        } else {
            char buf[TIMESPEC_LEN];
            (void)mvwaddstr(win, y, x,
                            timespec_str(&timedelta, buf, sizeof(buf)));
        }
    }
}

// FIXME:  Decouple this reporting from local PPS monitoring.
void pps_update(WINDOW *win, int y, int x)
{
    struct timedelta_t ppstimes;

    if (NULL == win) {
        return;
    }
    if (0 < pps_thread_ppsout(&session.pps_thread, &ppstimes)) {
        // NOTE: can not use double here due to precision requirements
        struct timespec timedelta;
        int i, ymax, xmax;

        getmaxyx(win, ymax, xmax);
        if (0 > ymax) {
            ymax = 0;  // squash a compiler warning
        }
        (void)wmove(win, y, x);
        // see toff_update() for explanation of the magic number
        for (i = 0; i < 18 && x + i < xmax - 1; i++) {
            (void)waddch(win, ' ');
        }
        TS_SUB( &timedelta, &ppstimes.clock, &ppstimes.real);
        // (long long) for 32-bit CPU with 64-bit time_t
        if (86400 < llabs(timedelta.tv_sec)) {
            // more than one day off, overflow
            // need a bigger field to show it
            (void)mvwaddstr(win, y, x, "> 1 day");
        } else {
            char buf[TIMESPEC_LEN];
            (void)mvwaddstr(win, y, x,
                            timespec_str(&timedelta, buf, sizeof(buf)));
        }
        (void)wnoutrefresh(win);
    }
}

/******************************************************************************
 *
 * Curses I/O
 *
 ******************************************************************************/

void monitor_fixframe(WINDOW * win)
{
    int ymax, xmax, ycur, xcur;

    if (NULL == win) {
        return;
    }
    getyx(win, ycur, xcur);
    getmaxyx(win, ymax, xmax);
    if (0 > xcur) {
        xcur = 0;  // squash a compiler warning
    }
    if (0 > ymax) {
        ymax = 0;  // squash a compiler warning
    }
    (void)mvwaddch(win, ycur, xmax - 1, ACS_VLINE);
}

static void packet_dump(const char *buf, size_t buflen)
{
    if (NULL != packetwin) {
        char buf2[MAX_PACKET_LENGTH * 2];

        cond_hexdump(buf2, buflen * 2, buf, buflen);
        printf( "Пакет %s", buf2);
//        (void)waddch(packetwin, (chtype)'\n');
    }
}

static void monitor_dump_send(const char *buf, size_t len)
{
    if (NULL != packetwin) {
        report_lock();
        packet_dump(buf, len);
        report_unlock();
    }
}

// log to the packet window if curses is up, otherwise stdout
static void gpsmon_report(const char *buf)
{
    // report locking is left to caller
   
        (void)fputs(buf, stdout);

    if (NULL != logfile) {
        (void)fputs(buf, logfile);
    }
}

static void announce_log(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 5, fmt, ap);
    va_end(ap);

   if (NULL != packetwin) {
        report_lock();
        (void)wattrset(packetwin, A_BOLD);
        (void)wprintw(packetwin, ">>>");
        (void)waddstr(packetwin, buf);
        (void)wattrset(packetwin, A_NORMAL);
        (void)wprintw(packetwin, "\n");
        report_unlock();
   }
   if (NULL != logfile) {
       (void)fprintf(logfile, ">>>%s\n", buf);
   }
}

static void monitor_vcomplain(const char *fmt, va_list ap)
{
    if (NULL == cmdwin) {
        return;
    }
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wattrset(cmdwin, A_BOLD);
    (void)vw_printw(cmdwin, fmt, ap);
    (void)wattrset(cmdwin, A_NORMAL);
    (void)wrefresh(cmdwin);
    (void)doupdate();

    (void)wgetch(cmdwin);
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wrefresh(cmdwin);
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)doupdate();
}

void monitor_complain(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    monitor_vcomplain(fmt, ap);
    va_end(ap);
}

void monitor_log(const char *fmt, ...)
{
    if (NULL != packetwin) {
        va_list ap;

        report_lock();
        va_start(ap, fmt);
        (void)vw_printw(packetwin, fmt, ap);
        va_end(ap);
        report_unlock();
    }
}

static const char *promptgen(void)
{
    static char buf[sizeof(session.gpsdata.dev.path) + HOST_NAME_MAX + 20];

    if (serial) {
        (void)snprintf(buf, sizeof(buf),
                       "%s:%s %u %u%c%u",
                       hostname,
                       session.gpsdata.dev.path,
                       session.gpsdata.dev.baudrate,
                       9 - session.gpsdata.dev.stopbits,
                       session.gpsdata.dev.parity,
                       session.gpsdata.dev.stopbits);
    } else {
        (void)strlcpy(buf, session.gpsdata.dev.path, sizeof(buf));
        if (NULL != source.device) {
            (void) strlcat(buf, ":", sizeof(buf));
            (void) strlcat(buf, source.device, sizeof(buf));
        }
    }
    return buf;
}

// refresh the device-identification window
static void refresh_statwin(void)
{
    if (NULL != session.device_type &&
        NULL != session.device_type->type_name) {
        type_name = session.device_type->type_name;
    } else {
        type_name = "Unknown device";
    }

    report_lock();
    (void)wclear(statwin);
    (void)wattrset(statwin, A_BOLD);
    (void)mvwaddstr(statwin, 0, 0, promptgen());
    (void)wattrset(statwin, A_NORMAL);
    (void)wnoutrefresh(statwin);
    report_unlock();
}

// refresh the command window
static void refresh_cmdwin(void)
{
    report_lock();
    (void)wmove(cmdwin, 0, 0);
    (void)wprintw(cmdwin, "%s", type_name);
    promptlen = strnlen(type_name, COLS);
    if (NULL != fallback &&
        0 != strcmp(fallback->type_name, type_name)) {
        (void)waddch(cmdwin, (chtype)' ');
        (void)waddch(cmdwin, (chtype)'(');
        (void)waddstr(cmdwin, fallback->type_name);
        (void)waddch(cmdwin, (chtype)')');
        promptlen += strnlen(fallback->type_name, COLS) + 3;
    }
    (void)wprintw(cmdwin, "> ");
    promptlen += 2;
    (void)wclrtoeol(cmdwin);
    (void)wnoutrefresh(cmdwin);
    report_unlock();
}

static bool curses_init(void)
{
    (void)initscr();
    (void)cbreak();
    (void)intrflush(stdscr, FALSE);
    (void)keypad(stdscr, true);
    (void)clearok(stdscr, true);
    (void)clear();
    (void)noecho();
    curses_active = true;

#define CMDWINHEIGHT    1

    statwin = newwin(CMDWINHEIGHT, 30, 0, 0);
    cmdwin = newwin(CMDWINHEIGHT, 0, 0, 30);
    packetwin = newwin(0, 0, CMDWINHEIGHT, 0);
    if (NULL == statwin ||
        NULL == cmdwin ||
        NULL == packetwin) {
        return false;
    }
    (void)scrollok(packetwin, true);
    (void)wsetscrreg(packetwin, 0, LINES - CMDWINHEIGHT);

    (void)wmove(packetwin, 0, 0);

    refresh_statwin();
    refresh_cmdwin();
    return true;
}

static bool switch_type(const struct gps_type_t *devtype)
{
    const struct monitor_object_t **trial, **newobject;
    newobject = NULL;
    for (trial = monitor_objects; *trial; trial++) {
        if (0 == strcmp((*trial)->driver->type_name, devtype->type_name)) {
            newobject = trial;
            break;
        }
    }
    if (newobject) {
        if (LINES < (*newobject)->min_y + 1 ||
            COLS < (*newobject)->min_x) {
            monitor_complain("%s requires %dx%d screen",
                             (*newobject)->driver->type_name,
                             (*newobject)->min_x, (*newobject)->min_y + 1);
        } else {
            int leftover;

            if (NULL != active) {
                if (NULL != (*active)->wrap) {
                    (*active)->wrap();
                }
                (void)delwin(devicewin);
                devicewin = NULL;
            }
            active = newobject;
            if (devicewin) {
                delwin(devicewin);
            }
            devicewin = newwin((*active)->min_y, (*active)->min_x, 1, 0);
            // screen might have JSON on it from the init sequence
            (void)clearok(stdscr, true);
            (void)clear();
            if (NULL == devicewin ||
                (NULL != (*active)->initialize &&
                 !(*active)->initialize())) {
                monitor_complain("Internal initialization failure - screen "
                                 "must be at least 80x24. Aborting.");
                return false;
            }

            leftover = LINES - 1 - (*active)->min_y;
            report_lock();
            if (0 >= leftover) {
                if (NULL != packetwin) {
                    (void)delwin(packetwin);
                }
                packetwin = NULL;
            } else if (NULL == packetwin) {
                packetwin = newwin(leftover, COLS, (*active)->min_y + 1, 0);
                (void)scrollok(packetwin, true);
                (void)wsetscrreg(packetwin, 0, leftover - 1);
            } else {
                (void)wresize(packetwin, leftover, COLS);
                (void)mvwin(packetwin, (*active)->min_y + 1, 0);
                (void)wsetscrreg(packetwin, 0, leftover - 1);
            }
            report_unlock();
        }
        return true;
    }

    monitor_complain("No monitor matches %s.", devtype->type_name);
    return false;
}

static void select_packet_monitor(struct gps_device_t *device)
{
    static int last_type = BAD_PACKET;

    /*
     * Switch display types on packet receipt.  Note, this *doesn't*
     * change the selection of the current device driver; that's done
     * within gpsd_multipoll() before this hook is called.
     */
    if (device->lexer.type != last_type) {
        const struct gps_type_t *active_type = device->device_type;
        if (NMEA_PACKET == device->lexer.type &&
            0 != ((device->device_type->flags & DRIVER_STICKY))) {
            active_type = &driver_nmea0183;
        }
        if (!switch_type(active_type)) {
            // driver switch failed?
            bailout = TERM_DRIVER_SWITCH;
        } else {
            refresh_statwin();
            refresh_cmdwin();
        }
        last_type = device->lexer.type;
    }

    if (NULL != active &&
        0 < device->lexer.outbuflen &&
        NULL != (*active)->update) {
        (*active)->update();
    }
    if (NULL != devicewin) {
        (void)wnoutrefresh(devicewin);
    }
}

// Control-L character
#define CTRL_L 0x0C

// char-by-char nonblocking input, return accumulated command line on \n
static char *curses_get_command(void)
{
    static char input[80];
    static char line[80];
    int c;

    c = wgetch(cmdwin);
    if (CTRL_L == c) {
        // ^L is to repaint the screen
        (void)clearok(stdscr, true);
        if (NULL != active &&
            NULL != (*active)->initialize) {
            (void)(*active)->initialize();
        }
    } else if ('\r' != c &&
               '\n' != c) {
        size_t len = strnlen(input, sizeof(input));

        if ('\b' == c ||
            KEY_LEFT == c ||
            (int)erasechar() == c) {
            input[len--] = '\0';
        } else if (isprint(c)) {
            input[len] = (char)c;
            input[++len] = '\0';
            (void)waddch(cmdwin, (chtype)c);
            (void)wrefresh(cmdwin);
            (void)doupdate();
        }

        return NULL;
    }

    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wrefresh(cmdwin);
    (void)doupdate();

    // user finished entering a command
    if ('\0' == input[0]) {
        return NULL;
    }

    (void) strlcpy(line, input, sizeof(line));
    input[0] = '\0';

    // handle it in the currently selected monitor object if possible
    if (serial &&
        NULL != active &&
        NULL != (*active)->command) {
        int status = (*active)->command(line);

        if (COMMAND_TERMINATE == status) {
            bailout = TERM_QUIT;
            return NULL;
        }
        if (COMMAND_MATCH == status ) {
            return NULL;
        }
        // FIXME: assert()s must die
        assert(status == COMMAND_UNKNOWN);
    }

    return line;
}

/******************************************************************************
 *
 * Mode-independent I/O
 *
 * Below this line, all calls to curses-dependent functions are guarded
 * by curses_active and have ttylike alternatives.
 *
 ******************************************************************************/

static ssize_t gpsmon_serial_write(struct gps_device_t *session,
                   const char *buf,
                   const size_t len)
// pass low-level data to devices, echoing it to the log window
{
    monitor_dump_send((const char *)buf, len);
    return gpsd_serial_write(session, buf, len);
}

bool monitor_control_send( unsigned char *buf, size_t len)
{
    ssize_t st;

    if (!serial) {
        return false;
    }

    context.readonly = false;
    st = session.device_type->control_send(&session, (char *)buf, len);
    context.readonly = true;
    return (st != -1);
}

static bool monitor_raw_send( unsigned char *buf, size_t len)
{
    ssize_t st = gpsd_write(&session, (char *)buf, len);
    return (0 < st &&
            (size_t)st == len);
}

static void complain(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    if (curses_active) {
        monitor_vcomplain(fmt, ap);
    } else {
        (void)vfprintf(stderr, fmt, ap);
        (void)fputc('\n', stderr);
    }

    va_end(ap);
}

/*****************************************************************************
 *
 * Main sequence
 *
 *****************************************************************************/

// per-packet hook
static void gpsmon_hook(struct gps_device_t *device, gps_mask_t changed UNUSED)
{
    char buf[BUFSIZ];

// FIXME:  If the following condition is false, the display is screwed up.
#if defined(SOCKET_EXPORT_ENABLE) && defined(PPS_DISPLAY_ENABLE)
    char ts_buf1[TIMESPEC_LEN];
    char ts_buf2[TIMESPEC_LEN];

    if (!serial &&
        str_starts_with((char*)device->lexer.outbuffer,
                        "{\"class\":\"TOFF\",")) {
        const char *end = NULL;
        int status = json_toff_read((const char *)device->lexer.outbuffer,
                                   &session.gpsdata,
                                   &end);

        if (0 != status) {
            complain("Ill-formed TOFF packet: %d (%s)", status,
                     json_error_string(status));
            return;
        }
        if (!curses_active) {
            (void)fprintf(stderr, "TOFF=%s real=%s\n",
                          timespec_str(&session.gpsdata.toff.clock,
                                       ts_buf1, sizeof(ts_buf1)),
                          timespec_str(&session.gpsdata.toff.real,
                                       ts_buf2, sizeof(ts_buf2)));
        }
        time_offset = session.gpsdata.toff;
        return;
    } else if (!serial &&
               str_starts_with((char*)device->lexer.outbuffer,
                               "{\"class\":\"PPS\",")) {
        const char *end = NULL;
        struct gps_data_t noclobber;
        struct timespec timedelta;
        char timedelta_str[TIMESPEC_LEN];
        int status = json_pps_read((const char *)device->lexer.outbuffer,
                                   &noclobber,
                                   &end);

        if (0 != status) {
            complain("Ill-formed PPS packet: %d (%s)", status,
                     json_error_string(status));
            return;
        }

        TS_SUB( &timedelta, &noclobber.pps.clock, &noclobber.pps.real);
        timespec_str(&timedelta, timedelta_str, sizeof(timedelta_str));

        if (!curses_active) {
            char pps_clock_str[TIMESPEC_LEN];
            char pps_real_str[TIMESPEC_LEN];

            timespec_str(&noclobber.pps.clock, pps_clock_str,
                         sizeof(pps_clock_str));
            timespec_str(&noclobber.pps.real, pps_real_str,
                         sizeof(pps_real_str));

            (void)fprintf(stderr,
                          "PPS=%.20s clock=%.20s offset=%.20s\n",
                          pps_clock_str,
                          pps_real_str,
                          timedelta_str);
        }

        (void)snprintf(buf, sizeof(buf),
                      "------------------- PPS offset: %.20s ------\n",
                      timedelta_str);
// FIXME:  Decouple this from the pps_thread code.
        /*
         * In direct mode this would be a bad idea, but we're not actually
         * watching for handshake events on a spawned thread here.
         */
        // coverity[missing_lock]
        session.pps_thread.pps_out = noclobber.pps;
        // coverity[missing_lock]
        session.pps_thread.ppsout_count++;
    } else
#endif // SOCKET_EXPORT_ENABLE && PPS_DISPLAY_ENABLE
    {
        size_t blen;

#ifdef __future__
        if (!serial) {
            if (JSON_PACKET == device->lexer.type) {
                const char *end = NULL;
                libgps_json_unpack((char *)device->lexer.outbuffer,
                                   &session.gpsdata, &end);
            }
        }
#endif // __future__

        if (curses_active) {
            select_packet_monitor(device);
        }

        (void)snprintf(buf, sizeof(buf), "(%d) ",
                       (int)device->lexer.outbuflen);
        blen  = strnlen(buf, sizeof(buf));
        cond_hexdump(buf + blen, sizeof(buf) - blen,
                     (char *)device->lexer.outbuffer,
                     device->lexer.outbuflen);
        (void)strlcat(buf, "\n", sizeof(buf));
    }

    report_lock();

    if (!curses_active) {
        (void)fputs(buf, stdout);
    } else {
        if (NULL != packetwin) {
            (void)waddstr(packetwin, buf);
            (void)wnoutrefresh(packetwin);
        }
        (void)doupdate();
    }

    if (NULL != logfile &&
        0 < device->lexer.outbuflen) {
        UNUSED size_t written_count = fwrite(device->lexer.outbuffer,
                                             sizeof(char),
                                             device->lexer.outbuflen,
                                             logfile);
        assert(written_count >= 1);
    }

    report_unlock();

    /* Update the last fix time seen for PPS if we've actually seen one,
     * and it is a new second. */
    if (0 >= device->newdata.time.tv_sec) {
        // "NTP: bad new time
    } else if (device->newdata.time.tv_sec <=
               device->pps_thread.fix_in.real.tv_sec) {
        // "NTP: Not a new time
    } else
        ntp_latch(device, &time_offset);
}

static bool do_command(const char *line)
{
    unsigned int v;
    struct timespec delay;
    unsigned char buf[BUFLEN];
    const char *arg;

    // skip over any spaces until NUL or the next argument
    for (arg = line + 1;
         *arg != '\0' &&
         isspace((unsigned char) *arg);
         arg++) {
    }

    switch (line[0]) {
    case 'c':   // change cycle time
        if (NULL == session.device_type) {
            complain("No device defined yet");
        } else if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            double rate = strtod(arg, NULL);
            const struct gps_type_t *switcher = session.device_type;

            if (NULL != fallback &&
                NULL != fallback->rate_switcher) {
                switcher = fallback;
            }
            if (NULL != switcher->rate_switcher) {
                // *INDENT-OFF*
                context.readonly = false;
                if (switcher->rate_switcher(&session, rate)) {
                    announce_log("[Rate switcher called.]");
                } else {
                    complain("Rate not supported.");
                }
                context.readonly = true;
                // *INDENT-ON*
            } else {
                complain("Device type %s has no rate switcher",
                         switcher->type_name);
            }
        }
        break;
    case 'i':   // start probing for subtype
        if (NULL == session.device_type) {
            complain("No GPS type detected.");
        } else if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            if (strcspn(line, "01") == strnlen(line, 4)) {
                context.readonly = !context.readonly;
            } else {
                context.readonly = (atoi(arg) == 0);
            }
            announce_log("[probing %sabled]", context.readonly ? "dis" : "en");
            if (!context.readonly) {
                // magic - forces a reconfigure
                session.lexer.counter = 0;
            }
        }
        break;

    case 'l':   // open logfile
        report_lock();
        if (NULL != logfile) {
            // close existing log, ignore argument
            if (NULL != packetwin) {
                (void)wprintw(packetwin, ">>> Logging off\n");
            }
            (void)fclose(logfile);
        } else if ('\0' != *arg) {
            logfile = fopen(arg, "a");
            // open new log file, arument is the name
            if (NULL != packetwin) {
                if (NULL != logfile) {
                    (void)wprintw(packetwin, ">>> Logging to %s\n", arg);
                } else {
                    (void)wprintw(packetwin, ">>> Logging to %s failed\n",
                                  arg);
                }
            }
        }
        report_unlock();
        break;

    case 'n':   // change mode
        // if argument not specified, toggle
        if (strcspn(line, "01") == strnlen(line, 4)) {
            v = (unsigned int)TEXTUAL_PACKET_TYPE(session.lexer.type);
        } else {
            v = (unsigned)atoi(arg);
        }
        if (NULL == session.device_type) {
            complain("No device defined yet");
        } else if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            const struct gps_type_t *switcher = session.device_type;

            if (NULL != fallback &&
                NULL != fallback->mode_switcher) {
                switcher = fallback;
            }
            if (NULL != switcher->mode_switcher) {
                context.readonly = false;
                announce_log("[Mode switcher to mode %d]", v);
                switcher->mode_switcher(&session, (int)v);
                context.readonly = true;
                (void)tcdrain(session.gpsdata.gps_fd);

                // wait 50,000 uSec
                delay.tv_sec = 0;
                delay.tv_nsec = 50000000L;
                nanosleep(&delay, NULL);

                /*
                 * Session device change will be set to NMEA when
                 * gpsmon resyncs.  So stash the current type to
                 * be restored if we do 'n' from NMEA mode.
                 */
                if (0 == v) {
                    fallback = switcher;
                }
            } else {
                complain("Device type %s has no mode switcher",
                         switcher->type_name);
            }
        }
        break;

    case 'q':   // quit
        return false;

    case 's':   // change speed
        if (NULL == session.device_type) {
            complain("No device defined yet");
        } else if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            speed_t speed;
            char parity = session.gpsdata.dev.parity;
            unsigned int stopbits =
                (unsigned int)session.gpsdata.dev.stopbits;
            char *modespec;
            const struct gps_type_t *switcher = session.device_type;

            if (NULL != fallback &&
                NULL != fallback->speed_switcher) {
                switcher = fallback;
            }
            modespec = strchr(arg, ':');
            if (NULL != modespec) {
                if (NULL == strchr("78", *++modespec)) {
                    complain("No support for that word length.");
                    break;
                }
                parity = *++modespec;
                if (NULL == strchr("NOE", parity)) {
                    complain("What parity is '%c'?.", parity);
                    break;
                }
                stopbits = (unsigned int)*++modespec;
                if (NULL == strchr("12", (char)stopbits)) {
                    complain("Stop bits must be 1 or 2.");
                    break;
                }
                stopbits = (unsigned int)(stopbits - '0');
            }
            speed = (unsigned)atoi(arg);
            if (switcher->speed_switcher) {
                context.readonly = false;
                if (switcher->speed_switcher(&session, speed,
                                             parity, (int)
                                             stopbits)) {
                    announce_log("[Speed switcher called.]");
                    /*
                     * See the comment attached to the 'DEVICE'
                     * command in gpsd.  Allow the control
                     * string time to register at the GPS
                     * before we do the baud rate switch,
                     * which effectively trashes the UART's
                     * buffer.
                     */
                    (void)tcdrain(session.gpsdata.gps_fd);
                    // wait 50,000 uSec
                    delay.tv_sec = 0;
                    delay.tv_nsec = 50000000L;
                    nanosleep(&delay, NULL);

                    (void)gpsd_set_speed(&session, speed,
                                         parity, stopbits);
                } else {
                    complain("Speed/mode combination not supported.");
                }
                context.readonly = true;
            } else {
                complain("Device type %s has no speed switcher",
                         switcher->type_name);
            }
            if (curses_active) {
                refresh_statwin();
            }
        }
        break;

    case 't':   // force device type
        if (!serial) {
            complain("Only available in low-level mode.");
        } else if (0 < strnlen(arg, 80)) {
            int matchcount = 0;
            const struct gps_type_t **dp, *forcetype = NULL;

            for (dp = gpsd_drivers; *dp; dp++) {
                if (NULL != strstr((*dp)->type_name, arg)) {
                    forcetype = *dp;
                    matchcount++;
                }
            }
            if (0 == matchcount) {
                complain("No driver type matches '%s'.", arg);
            } else if (1 == matchcount) {
                assert(forcetype != NULL);
                if (switch_type(forcetype)) {
                    (void)gpsd_switch_driver(&session,
                                             forcetype->type_name);
                }
                if (curses_active) {
                    refresh_cmdwin();
                }
            } else {
                complain("Multiple driver type names match '%s'.", arg);
            }
        }
        break;
    case 'x':   // send control packet
        if (NULL == session.device_type) {
            complain("No device defined yet");
        } else if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            ssize_t st = gps_hexpack(arg, buf, strnlen(arg, 1024));

            if (0 > st) {
                complain("Invalid hex string (error %zd)", st);
            } else if (NULL == session.device_type->control_send) {
                complain("Device type %s has no control-send method.",
                         session.device_type->type_name);
            } else if (!monitor_control_send(buf, (size_t)st)) {
                complain("Control send failed.");
            }
        }
        break;

    case 'X':   // send raw packet
        if (!serial) {
            complain("Only available in low-level mode.");
        } else {
            ssize_t len = gps_hexpack(arg, buf, strnlen(arg,  1024));

            if (0 > len) {
                complain("Invalid hex string (error %zd)", len);
            } else if (!monitor_raw_send(buf, (size_t)len)) {
                complain("Raw send failed.");
            }
        }
        break;

    default:
        complain("Unknown command '%c'", line[0]);
        break;
    }

    // continue accepting commands
    return true;
}

static char *pps_report(volatile struct pps_thread_t *pps_thread UNUSED,
                        struct timedelta_t *td UNUSED) {
    report_lock();
    gpsmon_report(PPSBAR);
    report_unlock();
    return "gpsmon";
}

/* Handle sigals.  All we can really do in a signal handler is mark it
 * and return.
 */
static void onsig(int sig UNUSED) {
    if (SIGABRT == sig) {
        bailout = TERM_QUIT;
    } else {
        bailout = TERM_SIGNAL;
    }
}

#define WATCHRAW        "?WATCH={\"raw\":2,\"pps\":true}\r\n"
#define WATCHRAWDEVICE  "?WATCH={\"raw\":2,\"pps\":true,\"device\":\"%s\"}\r\n"
#define WATCHNMEA       "?WATCH={\"nmea\":true,\"pps\":true}\r\n"
#define WATCHNMEADEVICE "?WATCH={\"nmea\":true,\"pps\":true,\"device\":\"%s\"}\r\n"

// this placement avoids a compiler warning
static const char *cmdline;

static void usage(void)
{
    (void)fputs(
         "usage: gpsmon [OPTIONS] [server[:port:[device]]]\n\n"
#ifdef HAVE_GETOPT_LONG
         "  --debug DEBUGLEVEL  Set DEBUGLEVEL\n"
         "  --help              Show this help, then exit\n"
         "  --list              List known device types, then exit.\n"
         "  --logfile FILE      Log to LOGFILE\n"
         "  --nocurses          No curses. Data only.\n"
         "  --nmea              Force NMEA mode.\n"
         "  --type TYPE         Set receiver TYPE\n"
         "  --version           Show version, then exit\n"
#endif
         "  -a                  No curses. Data only.\n"
         "  -?                  Show this help, then exit\n"
         "  -D DEBUGLEVEL       Set DEBUGLEVEL\n"
         "  -h                  Show this help, then exit\n"
         "  -L                  List known device types, then exit.\n"
         "  -l FILE             Log to LOGFILE\n"
         "  -n                  Force NMEA mode.\n"
         "  -t TYPE             Set receiver TYPE\n"
         "  -V                  Show version, then exit\n",
         stderr);
}

int main(int argc, char **argv)
{
    int ch;
    char *explanation;
    int matches = 0;
    bool nmea = false;
    fd_set all_fds;
    fd_set rfds;
    volatile socket_t maxfd = 0;
    char inbuf[80];
    volatile bool nocurses = false;
    int activated = -1;
    const char *optstring = "?aD:hLl:nt:V";
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {
        {"debug", required_argument, NULL, 'D'},
        {"help", no_argument, NULL, 'h'},
        {"list", no_argument, NULL, 'L' },
        {"logfile", required_argument, NULL, 'l'},
        {"nmea", no_argument, NULL, 'n' },
        {"nocurses", no_argument, NULL, 'a' },
        {"type", required_argument, NULL, 't'},
        {"version", no_argument, NULL, 'V' },
        {NULL, 0, NULL, 0},
    };
#endif

    gethostname(hostname, sizeof(hostname) - 1);
    (void)putenv("TZ=UTC");     // for ctime()
    gps_context_init(&context, "gpsmon");       // initialize the report mutex
    context.serial_write = gpsmon_serial_write;
    context.errout.report = gpsmon_report;
    while (1) {
#ifdef HAVE_GETOPT_LONG
        ch = getopt_long(argc, argv, optstring, long_options, &option_index);
#else
        ch = getopt(argc, argv, optstring);
#endif

        if (ch == -1) {
            break;
        }

        switch (ch) {
        case 'a':
            nocurses = true;
            break;
        case 'D':
            context.errout.debug = atoi(optarg);
            json_enable_debug(context.errout.debug - 2, stderr);
            break;
        case 'L':               // list known device types
            (void)
                fputs
                ("General commands available per type. '+' "
                 "means there are private commands.\n",
                 stdout);
            for (active = monitor_objects; *active; active++) {
                (void)fputs("i l q ^S ^Q", stdout);
                (void)fputc(' ', stdout);
                if (NULL != (*active)->driver->mode_switcher) {
                    (void)fputc('n', stdout);
                } else {
                    (void)fputc(' ', stdout);
                }
                (void)fputc(' ', stdout);
                if (NULL != (*active)->driver->speed_switcher) {
                    (void)fputc('s', stdout);
                } else {
                    (void)fputc(' ', stdout);
                }
                (void)fputc(' ', stdout);
                if (NULL != (*active)->driver->rate_switcher) {
                    (void)fputc('x', stdout);
                } else {
                    (void)fputc(' ', stdout);
                }
                (void)fputc(' ', stdout);
                if (NULL != (*active)->driver->control_send) {
                    (void)fputc('x', stdout);
                } else {
                    (void)fputc(' ', stdout);
                }
                (void)fputc(' ', stdout);
                if (NULL != (*active)->command) {
                    (void)fputc('+', stdout);
                } else {
                    (void)fputc(' ', stdout);
                }
                (void)fputs("\t", stdout);
                (void)fputs((*active)->driver->type_name, stdout);
                (void)fputc('\n', stdout);
            }
            exit(EXIT_SUCCESS);
        case 'l':               // enable logging at startup
            logfile = fopen(optarg, "w");
            if (NULL == logfile) {
                (void)fprintf(stderr, "Couldn't open logfile for writing.\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'n':
            nmea = true;
            break;
        case 't':
            fallback = NULL;
            for (active = monitor_objects; *active; active++) {
                if (str_starts_with((*active)->driver->type_name, optarg)) {
                    fallback = (*active)->driver;
                    matches++;
                }
            }
            if (1 < matches) {
                (void)fprintf(stderr,
                              "-t option matched more than one driver.\n");
                exit(EXIT_FAILURE);
            }
            if (0 == matches) {
                (void)fprintf(stderr, "-t option didn't match any driver.\n");
                exit(EXIT_FAILURE);
            }
            active = NULL;
            break;
        case 'V':
            (void)printf("%s: %s (revision %s)\n", argv[0], VERSION, REVISION);
            exit(EXIT_SUCCESS);
        case 'h':
            FALLTHROUGH
        case '?':
            usage();
            exit(EXIT_SUCCESS);
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }

    gpsd_time_init(&context, time(NULL));
    gpsd_init(&session, &context, NULL);

    // Grok the server, port, and device.
    if (optind < argc) {
        serial = str_starts_with(argv[optind], "/dev");
        gpsd_source_spec(argv[optind], &source);
    } else {
        serial = false;
        gpsd_source_spec(NULL, &source);
    }

    if (serial) {
        if (NULL == source.device) {
            // this can happen with "gpsmon /dev:dd"
            (void) strlcpy(session.gpsdata.dev.path,
                           argv[optind],
                           sizeof(session.gpsdata.dev.path));
        } else {
            (void) strlcpy(session.gpsdata.dev.path,
                           source.device,
                           sizeof(session.gpsdata.dev.path));
        }
    } else {
        if (0 == strstr(source.server, "//")) {
            (void) strlcpy(session.gpsdata.dev.path,
                           "tcp://",
                           sizeof(session.gpsdata.dev.path));
        } else {
            session.gpsdata.dev.path[0] = '\0';
        }
        str_appendf(session.gpsdata.dev.path, sizeof(session.gpsdata.dev.path),
                       "%s:%s", source.server, source.port);
    }

    activated = gpsd_activate(&session, O_PROBEONLY);
    if (0 > activated) {
        if (PLACEHOLDING_FD == activated) {
                (void)fputs("gpsmon:ERROR: PPS device unsupported\n", stderr);
        }
        exit(EXIT_FAILURE);
    }

    if (serial) {
        // this guard suppresses a warning on Bluetooth devices
        if (SOURCE_RS232 == session.sourcetype ||
            SOURCE_ACM == session.sourcetype ||
            SOURCE_USB == session.sourcetype ) {
            session.pps_thread.report_hook = pps_report;
#ifdef MAGIC_HAT_ENABLE
            /*
             * The HAT kludge. If we're using the HAT GPS on a
             * Raspberry Pi or a workalike like the ODROIDC2, and
             * there is a static "first PPS", and we have access because
             * we're root, assume we want to use KPPS.
             */
            if (0 == strcmp(session.pps_thread.devicename, MAGIC_HAT_GPS) ||
                0 == strcmp(session.pps_thread.devicename, MAGIC_LINK_GPS)) {
                const char *first_pps = pps_get_first();

                if (0 == access(first_pps, R_OK | W_OK)) {
                        session.pps_thread.devicename = first_pps;
                }
            }
#endif  // MAGIC_HAT_ENABLE
            pps_thread_activate(&session.pps_thread);
        }
    } else if (NULL != source.device) {
        (void)gps_send(&session.gpsdata,
                       nmea ? WATCHNMEADEVICE : WATCHRAWDEVICE, source.device);
    } else {
        (void)gps_send(&session.gpsdata, nmea ? WATCHNMEA : WATCHRAW);
    }

    /*
     * This is a monitoring utility. Disable autoprobing, because
     * in some cases (e.g. SiRFs) there is no way to probe a chip
     * type without flipping it to native mode.
     */
    context.readonly = true;

    FD_ZERO(&all_fds);
#ifndef __clang_analyzer__
    FD_SET(0, &all_fds);        // accept keystroke inputs
#endif // __clang_analyzer__


    FD_SET(session.gpsdata.gps_fd, &all_fds);
    if (session.gpsdata.gps_fd > maxfd) {
         maxfd = session.gpsdata.gps_fd;
    }

    // quit cleanly if we get a signal
    (void)signal(SIGABRT, onsig);
    (void)signal(SIGQUIT, onsig);
    (void)signal(SIGINT, onsig);
    (void)signal(SIGTERM, onsig);

    if (nocurses) {
        (void)fputs("gpsmon: ", stdout);
        (void)fputs(promptgen(), stdout);
        (void)fputs("\n", stdout);
        (void)tcgetattr(0, &cooked);
        (void)tcgetattr(0, &rare);
        rare.c_lflag &=~ (ICANON | ECHO);
        rare.c_cc[VMIN] = (cc_t)1;
        (void)tcflush(0, TCIFLUSH);
        (void)tcsetattr(0, TCSANOW, &rare);
    } else if (!curses_init()) {
        // curses failed to init!
        bailout = TERM_CURSES;
    }

    // The main loop, stay here until near the end
    // check bailout frequently as that is async to the loop.
    for (;;) {
        fd_set efds;

        // check for any SIGNAL;
        if (0 != bailout) {
            break;
        }
        timespec_t ts_timeout = {2, 0};   // timeout for pselect()

        switch(gpsd_await_data(&rfds, &efds, maxfd, &all_fds,
                               &context.errout, ts_timeout)) {
        case AWAIT_GOT_INPUT:
            FALLTHROUGH
        case AWAIT_TIMEOUT:
            break;
        case AWAIT_NOT_READY:
            // no recovery from bad fd is possible
            if (FD_ISSET(session.gpsdata.gps_fd, &efds)) {
                bailout = TERM_SELECT_FAILED;
                break;
            }
            continue;
        case AWAIT_FAILED:
            bailout = TERM_SELECT_FAILED;
            break;
        }

        // check for any SIGNAL;
        if (0 != bailout) {
            break;
        }

        switch(gpsd_multipoll(FD_ISSET(session.gpsdata.gps_fd, &rfds),
                              &session, gpsmon_hook, 0)) {
        case DEVICE_READY:
            FD_SET(session.gpsdata.gps_fd, &all_fds);
            break;
        case DEVICE_UNREADY:
            bailout = TERM_EMPTY_READ;
            break;
        case DEVICE_ERROR:
            bailout = TERM_READ_ERROR;
            break;
        case DEVICE_EOF:
            bailout = TERM_QUIT;
            break;
        default:
            break;
        }

        // check for any SIGNAL;
        if (0 != bailout) {
            break;
        }

        if (FD_ISSET(0, &rfds)) {
            if (curses_active) {
                cmdline = curses_get_command();
            } else {
                // coverity[string_null_argument]
                ssize_t st = read(0, &inbuf, 1);

                if (1 == st) {
                    report_lock();
                    (void)tcflush(0, TCIFLUSH);
                    (void)tcsetattr(0, TCSANOW, &cooked);
                    (void)fputs("gpsmon: ", stdout);
                    (void)fputs(promptgen(), stdout);
                    (void)fputs("> ", stdout);
                    (void)putchar(inbuf[0]);
                    cmdline = fgets(inbuf + 1, sizeof(inbuf) - 1, stdin);
                    if (cmdline) {
                        cmdline--;
                    }
                    report_unlock();
                }
            }
            if (NULL != cmdline &&
                !do_command(cmdline)) {
                bailout = TERM_QUIT;
            }
            // check for any SIGNAL;
            if (0 != bailout) {
                break;
            }

            if (!curses_active) {
                (void)sleep(2);
                report_lock();
                (void)tcsetattr(0, TCSANOW, &rare);
                report_unlock();
            }
            // check for any SIGNAL;
            if (0 != bailout) {
                break;
            }
        }
    }

    // Something bad happened, we fell out of the loop.

    // Shut down PPS monitoring.
    if (serial) {
       (void)pps_thread_deactivate(&session.pps_thread);
    }

    gpsd_close(&session);
    if (logfile) {
        (void)fclose(logfile);
    }
    if (curses_active) {
        (void)endwin();
    } else {
        (void)tcsetattr(0, TCSANOW, &cooked);
    }

    explanation = NULL;
    switch (bailout) {
    case TERM_CURSES:
        explanation = "curses_init() failed\n";
        break;
    case TERM_DRIVER_SWITCH:
        explanation = "Driver type switch failed\n";
        break;
    case TERM_EMPTY_READ:
        explanation = "Device went offline\n";
        break;
    case TERM_READ_ERROR:
        explanation = "Read error from device\n";
        break;
    case TERM_SELECT_FAILED:
        explanation = "I/O wait on device failed\n";
        break;
    case TERM_SIGNAL:
        FALLTHROUGH
    case TERM_QUIT:
        // normal exit, no message
        break;
    default:
        explanation = "Unknown error, should never happen.\n";
        break;
    }

    if (curses_active) {
        (void)endwin();
    }
    if (NULL != explanation) {
        (void)fputs(explanation, stderr);
    }
    if (logfile) {
        (void)fclose(logfile);
    }
    exit(EXIT_SUCCESS);
}

/* pastef() - prints n/a or finite float at a point in a window 
 *
 * win: Pointer to the window to print in
 * y: The row in the window
 * x: The start colmun in that row
 * flen: a leghth for to end of the n/a (should match fmt)
 * fmt: a printf(3) style format string for f
 * f: a ieee-754 double float (prefereably finite).
 *
 * returns: Noyhing as void
 *
 * caveat: flen is not passed if f is finite, write fmt with all
 *         numbers hardcoded.
 */
void pastef(WINDOW *win, int y, int x, int flen, char *fmt, double f) {
    if (0 != isfinite(f)) {
        (void)mvwprintw(win, y, x, fmt, f);
    } else {
        (void)mvwprintw(win, y, x, "%*s", flen, "n/a");
    }
}

// gpsmon.c ends here
// vim: set expandtab shiftwidth=4
