/*
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "include/gpsd_config.h"  // must be before all includes

#include <math.h>
#include <stdint.h>           // for int64_t (tow in display_ubx_nav)
#include <stdlib.h>           // for labs()
#include <string.h>           // for memset()
#include <time.h>
#include <stdio.h>
#include "libpq-fe.h"

#include "include/gpsd.h"
#include "include/bits.h"
#include "include/gpsmon.h"

#include "include/driver_ubx.h"
extern const struct gps_type_t driver_ubx;
static WINDOW *satwin, *navsolwin, *dopwin;

#define display (void)mvwprintw

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

static bool ubx_initialize(void)
{
    int i;

    // "heavily inspired" by monitor_nmea.c
    if (NULL == (satwin = derwin(devicewin, 19, 28, 0, 0))) {
        return false;
    }


    return true;
}


#define MAXSKYCHANS 16
static void display_nav_svinfo(unsigned char *buf, size_t data_len)
{
    int az, el, i, nchan;
    unsigned fl, off, prn, ss;

    // very coarse sanity check (minimal length for valid message reached?)
    if (data_len < 8)
        return;

    nchan = getub(buf, 4);
    if (nchan > MAXSKYCHANS)
        nchan = MAXSKYCHANS;

    for (i = 0; i < nchan; i++) {
        off = 8 + 12 * i;

        prn = getub(buf, off + 1);
        fl = getleu16(buf, off + 2);
        ss = getub(buf, off + 4);
        el = getsb(buf, off + 5);
        az = getles16(buf, off + 6);
        (void)mvwprintw(satwin, i + 2,  4, "%3d %3d %3d  %2d %04x %c",
                        prn, az, el, ss, fl, (fl & UBX_SAT_USED) ? 'Y' : ' ');
    }
    // clear potentially stale sat lines unconditionally
    for (;i < MAXSKYCHANS; i++) {
        mvwprintw(satwin, (int)(i + 2), 4, "%22s", "");
    }

    // update pane label, in case NAV-SAT was previously displayed
    (void)wattrset(satwin, A_BOLD);
    display(satwin, 18, 13, "VINFO ");
    (void)wattrset(satwin, A_NORMAL);
    (void)wnoutrefresh(satwin);
    return;
}


static void display_nav_sat(unsigned char *buf, size_t data_len)
{
    int az, el, i, nchan;
    unsigned fl, gnss, off, prn, ss;

    // very coarse sanity check (minimal length for valid message reached?)
    if (data_len < 8) {
        return;
    }

    nchan = getub(buf, 5);
    if (nchan > MAXSKYCHANS) {
        nchan = MAXSKYCHANS;
    }

#define SV session.gpsdata.skyview[i]
    for (i = 0; i < nchan; i++) {
        off = 8 + 12 * i;
        gnss = getub(buf, off);
        prn = getub(buf, off + 1);
        fl = getleu16(buf, off + 8);
        ss = getub(buf, off + 2);
        el = getsb(buf, off + 3);
        az = getles16(buf, off + 4);

        // Translate sat numbering to the one used in UBX-NAV-SVINFO
        if (gnss == 2) {
            prn += 210;  // Galileo
        } else if (gnss == 3 && prn <= 5) {
            prn += 158;  // BeiDou
        } else if (gnss == 3 && prn >= 6) {
            prn += 27;   // BeiDou (continued)
        } else if (gnss == 4) {
            prn += 172;  // IMES
        } else if (gnss == 5) {
            prn += 192;  // QZSS
        } else if (gnss == 6 && prn != 255) {
            prn += 64;   // GLONASS
        }

        (void)mvwprintw(satwin, i + 2, 4, "%3d %3d %3d  %2d %04x %c",
                        prn, az, el, ss, fl,
                        (fl & (UBX_SAT_USED << 3)) ? 'Y' : ' ');
    }
    printf( " спутников %2d ", session.gpsdata.satellites_used);
    printf (" доп %5.1f ", session.gpsdata.dop.pdop);
#undef SV

    // clear potentially stale sat lines unconditionally
    for (; i < MAXSKYCHANS; i++) {
        (void)mvwprintw(satwin, i + 2,  4, "%22s", "");
    }
#undef MAXSKYCHANS

    // update pane label, in case NAV-SAT was previously displayed
    (void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0), (void)syncok(satwin, true);
    (void)wattrset(satwin, A_BOLD);
    display(satwin, 18, 7, " NAV-SAT ");
    (void)wattrset(satwin, A_NORMAL);
    (void)wnoutrefresh(satwin);
 

    return;
}


static void display_nav_dop(unsigned char *buf, size_t data_len)
{
    if (data_len != 18) {
        return;
    }
    pastef(dopwin, 1,  9, 3, "%4.1f", getleu16(buf, 12) / 100.0);
    pastef(dopwin, 1, 18, 3, "%4.1f", getleu16(buf, 10) / 100.0);
    pastef(dopwin, 1, 27, 3, "%4.1f", getleu16(buf,  6) / 100.0);
    pastef(dopwin, 1, 36, 3, "%4.1f", getleu16(buf,  8) / 100.0);
    pastef(dopwin, 1, 45, 3, "%4.1f", getleu16(buf,  4) / 100.0);
}


static void display_nav_sol(unsigned char *buf, size_t data_len)
{
    gps_mask_t outmask;
    unsigned short gw = 0;
    unsigned int tow = 0, flags;
    double epx, epy, epz, evx, evy, evz;
    unsigned char navmode;
    struct gps_data_t g;

    if (52 != data_len) {
        return;
    }
    // pacify coverity
    memset(&g, 0, sizeof(g));

navmode = (unsigned char)getub(buf, 10);
    flags = (unsigned int)getub(buf, 11);

    if ((flags & (UBX_SOL_VALID_WEEK | UBX_SOL_VALID_TIME)) != 0) {
        tow = (unsigned int)getleu32(buf, 0);
        gw = (unsigned short)getles16(buf, 8);
    }

    epx = (double)(getles32(buf, 12) / 100.0);
    epy = (double)(getles32(buf, 16) / 100.0);
    epz = (double)(getles32(buf, 20) / 100.0);
    evx = (double)(getles32(buf, 28) / 100.0);
    evy = (double)(getles32(buf, 32) / 100.0);
    evz = (double)(getles32(buf, 36) / 100.0);
    outmask = ecef_to_wgs84fix(&g.fix, epx, epy, epz, evx, evy, evz);

    g.fix.epx = g.fix.epy = (double)(getles32(buf, 24) / 100.0);
    g.fix.eps = (double)(getles32(buf, 40) / 100.0);
    g.dop.pdop = (double)(getleu16(buf, 44) / 100.0);
    g.satellites_used = (int)getub(buf, 47);

    printf( "epx %+10.2f epz %+10.2f evx %+9.2f evy %+9.2f evz %+9.2f ", epx, epz, evx, evy, evz);

    if (0 != (outmask & LATLON_SET)) {
        printf("φ %12.9f  λ %13.9f  h %8.2fm ",
                  g.fix.latitude, g.fix.longitude, g.fix.altHAE);
    }
  
    // coverity says g.fix.track never set.
    if (0 != (outmask & VNED_SET)) {
        printf("%6.2fm/s %5.1fo %6.2fm/s ",
                  g.fix.speed, NAN, g.fix.climb);
    }


    {
        uint64_t tod = tow / 1000UL;              // remove ms
        unsigned s = (unsigned)(tod % 60);
        unsigned m = (unsigned)((tod % 3600UL) / 60);
        unsigned h = (unsigned)((tod / 3600UL) % 24);
        unsigned day = (unsigned)(tod / 86400UL);

        printf ("Дата %u %02u:%02u:%02d.%02d ",
                        day, h, m, s, (tow % 1000)  / 10);    
    }
    if ((flags & (UBX_SOL_VALID_WEEK | UBX_SOL_VALID_TIME)) != 0) {
        printf ("%d+%10.3lf tow %d", gw, (double)(tow / 1000.0), (tow / 86400000));
    }

    // relies on the fact that epx and epy are set to same value
    printf ("%7.2f epx %6.2f epv %2d sputn %5.1f pdop 0x%02x navmod 0x%02x flag", g.fix.epx, g.fix.epv, g.satellites_used, g.dop.pdop, navmode, flags);
    
    printf("\r\n");
    
    {
    
	char			   *conninfo = NULL;
	char			   *seria = "?";
	PGconn			   *conn;
	PGresult		   *res;
	int					nFields;
	int					i,
						j;
    uint64_t tod = tow / 1000UL;              // remove ms
        unsigned s = (unsigned)(tod % 60);
        unsigned m = (unsigned)((tod % 3600UL) / 60);
        unsigned h = (unsigned)((tod / 3600UL) % 24);
        unsigned day = (unsigned)(tod / 86400UL);						
	char stmt[512]; /* максимальная длина запроса */
    if (0 == (outmask & LATLON_SET)) {
                  g.fix.latitude = NAN;
                  g.fix.longitude = NAN;
                  g.fix.altHAE = NAN;
    }
 
    // coverity says g.fix.track never set.
    if (0 == (outmask & VNED_SET)) {
        g.fix.speed = NAN;
        g.fix.climb = NAN;
    }	
	const char		   *query1 =
		"INSERT INTO \"Измерения\".\"U-Blox\" (\"Серия\", φ, λ, h, epx, epz, evx, evy, evz, v, clm, \"День недели\", \"UTC\", epx1, epv, \"Спутников\", dop, \"Режим\", flg)"
		" VALUES('%s', '%12.9f', '%13.9f', '%8.2f', '%+10.2f', '%+10.2f', '%+9.2f', '%+9.2f', '%+9.2f', '%6.2f', '%6.2f', %u, '%02u:%02u:%02d.%02d', '%7.2f', '%6.2f', %d, '%5.1f', %d, '%02x') RETURNING *;";
	
	seria = "ßßßß";	
		conninfo = "dbname='Геоинформационная система' host=localhost port=5432 connect_timeout=10 password=111111";

	/* Соединяемся с БД */
	conn = PQconnectdb(conninfo);

	/* Проверка провала соединения с БД */
	if (PQstatus(conn) != CONNECTION_OK)
	{
		fprintf(stderr, "%s", PQerrorMessage(conn));
		PQfinish(conn);
	}
	
	sprintf(stmt, query1,
        	seria, // Серия, φ, λ, h, 
        	g.fix.latitude,
        	g.fix.longitude,
        	g.fix.altHAE,
        	epx, epz, evx, evy, evz, //epx, epz, evx, evy, evz, v, clm, d, t, epx1, epv, sptn, dop, mode
            g.fix.speed, g.fix.climb,
            day, h, m, s,(tow % 1000)  / 10,
            g.fix.epx, g.fix.epv,
            g.satellites_used, g.dop.pdop, navmode, flags       	
        );
printf("SQL\n");
printf(stmt);
printf("\n");
		/* Отправка текста запроса в БД */
		res = PQexec(conn,stmt);
		if(PQresultStatus(res) != PGRES_COMMAND_OK)
			fprintf(stderr, "%s", PQerrorMessage(conn));


	PQfinish(conn);
    }
}

static void ubx_update(void)
{
    unsigned char *buf;
    size_t data_len;
    unsigned short msgid;

    buf = session.lexer.outbuffer;
    msgid = (unsigned short)((buf[2] << 8) | buf[3]);
    data_len = (size_t) getles16(buf, 4);
    switch (msgid) {
    case UBX_NAV_SVINFO:
        display_nav_svinfo(&buf[6], data_len);
        break;
    case UBX_NAV_SAT:
        display_nav_sat(&buf[6], data_len);
        break;
    case UBX_NAV_DOP:
        display_nav_dop(&buf[6], data_len);
        break;
    case UBX_NAV_SOL:
        display_nav_sol(&buf[6], data_len);
        break;
    default:
        break;
    }

   ;
}

static int ubx_command(char line[]UNUSED)
{
    return COMMAND_UNKNOWN;
}

static void ubx_wrap(void)
{
    (void)delwin(satwin);
    return;
}
const struct monitor_object_t ubx_mmt = {
    .initialize = ubx_initialize,
    .update = ubx_update,
    .command = ubx_command,
    .wrap = ubx_wrap,
    .min_y = 19,.min_x = 80,    // size of the device window
    .driver = &driver_ubx,
};

// vim: set expandtab shiftwidth=4
