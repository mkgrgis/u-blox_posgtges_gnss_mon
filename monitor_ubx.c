/*
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "include/gpsd_config.h"  // must be before all includes

#include <math.h>
#include <stdint.h>		   // for int64_t (tow in display_ubx_nav)
#include <stdlib.h>		   // for labs()
#include <string.h>		   // for memset()
#include <time.h>
#include <stdio.h>
#include "libpq-fe.h"

#include "include/gpsd.h"
#include "include/bits.h"
#include "include/gpsmon.h"

#include "include/driver_ubx.h"
extern const struct gps_type_t driver_ubx;

extern PGconn *pg_conn;
extern char *postgres_seria_str;

static unsigned int tow = 0; /* time of week ? */
static unsigned t_s, t_m, t_h, t_ms, t_d;

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

static void display_nav_svinfo(unsigned char *buf, size_t data_len)
{
	int az, el, i, nchan;
	unsigned fl, off, prn, ss;
	bool ok_sat;
	PGconn			   *conn;
	PGresult		   *res;				
	char stmt[512]; /* максимальная длина запроса */	
	
	char *q_base = "INSERT INTO \"Измерения\".\"U-Blox-спутники\" (\"Серия\", \"UTC\", prn, az, el, ss, fl, ok_sat)"
		" VALUES('%s', '%02u:%02u:%02d.%02d', %3d, %3d, %3d, %2d, '%04x', %d) RETURNING *;";	

	// very coarse sanity check (minimal length for valid message reached?)
	if (data_len < 8)
		return;

	nchan = getub(buf, 4);

	tow = (unsigned int)getleu32(buf, 0);

	for (i = 0; i < nchan; i++) {
		off = 8 + 12 * i;

		prn = getub(buf, off + 1);
		fl = getleu16(buf, off + 2);
		ss = getub(buf, off + 4);
		el = getsb(buf, off + 5);
		az = getles16(buf, off + 6);
		ok_sat = (fl & (UBX_SAT_USED << 3));
		
		printf("%d prn %3d az %3d el %3d  ss %2d fl %04x U %c", i, 
						prn, az, el, ss, fl, (fl & UBX_SAT_USED) ? 'Y' : ' ');
		sprintf(stmt, q_base,
			  	postgres_seria_str,
				   t_h, t_m, t_s, t_ms,
				prn, az, el, ss, fl, ok_sat
			);
		printf("SQL sat0 --: ");
		printf(stmt);
		printf("\n");
		/* Отправка текста запроса в БД */
		res = PQexec(pg_conn,stmt);
		if(PQresultStatus(res) != PGRES_COMMAND_OK)
			fprintf(stderr, "%s", PQerrorMessage(conn));						
	}

	return;	
}


static void display_nav_sat(unsigned char *buf, size_t data_len)
{
	int az, el, i, nchan;
	unsigned fl, gnss, off, prn, ss;
	bool ok_sat;
	PGconn			   *conn;
	PGresult		   *res;				
	char stmt[512]; /* максимальная длина запроса */	
	
	char *q_base = "INSERT INTO \"Измерения\".\"U-Blox-спутники\" (\"Серия\", \"UTC\",prn, az, el, ss, fl, ok_sat)"
		" VALUES('%s', '%02u:%02u:%02d.%02d', %3d, %3d, %3d, %2d, '%04x', %d) RETURNING *;";

	// very coarse sanity check (minimal length for valid message reached?)
	if (data_len < 8) {
		return;
	}

	nchan = getub(buf, 5);

	tow = (unsigned int)getleu32(buf, 0);

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
		ok_sat = (fl & (UBX_SAT_USED << 3));

		printf("что %3d az %3d el %3d  ss %2d fl %04x U %c",
						prn, az, el, ss, fl,
						ok_sat ? 'Y' : ' ');
		sprintf(stmt, q_base,
			   	postgres_seria_str,
				   t_h, t_m, t_s, t_ms,
				prn, az, el, ss, fl, ok_sat
			);
		printf("SQL sat --: ");
		printf(stmt);
		printf("\n");
		/* Отправка текста запроса в БД */
		res = PQexec(pg_conn,stmt);
		if(PQresultStatus(res) != PGRES_COMMAND_OK)
			fprintf(stderr, "%s", PQerrorMessage(conn));						
	}
	printf( " спутников %2d ", session.gpsdata.satellites_used);
	printf (" доп %5.1f ", session.gpsdata.dop.pdop);
#undef SV

	return;
}


static void display_nav_dop(unsigned char *buf, size_t data_len)
{
	if (data_len != 18) {
		return;
	}
	printf("d1 %4.1f", getleu16(buf, 12) / 100.0);
	printf("d2 %4.1f", getleu16(buf, 10) / 100.0);
	printf("d3 %4.1f", getleu16(buf,  6) / 100.0);
	printf("d4 %4.1f", getleu16(buf,  8) / 100.0);
	printf("d5 %4.1f", getleu16(buf,  4) / 100.0);
}


static void display_nav_sol(unsigned char *buf, size_t data_len)
{
	gps_mask_t outmask;
	unsigned short gw = 0;
	unsigned int flags;
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
		{
			uint64_t tod = tow / 1000UL;			  // remove ms
			t_s = (unsigned)(tod % 60);
			t_m = (unsigned)((tod % 3600UL) / 60);
			t_h = (unsigned)((tod / 3600UL) % 24);
			t_ms = (tow % 1000)  / 10;
			t_d = (unsigned)(tod / 86400UL); // День недели
		}

		gw = (unsigned short)getles16(buf, 8);
		printf ("Дата %u %02u:%02u:%02d.%02d ",
				t_d, t_h, t_m, t_s, t_ms);	
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

	if ((flags & (UBX_SOL_VALID_WEEK | UBX_SOL_VALID_TIME)) != 0) {
		printf ("%d+%10.3lf tow %d", gw, (double)(tow / 1000.0), (tow / 86400000));
	}

	// relies on the fact that epx and epy are set to same value
	printf ("%7.2f epx %6.2f epv %2d sputn %5.1f pdop 0x%02x navmod 0x%02x flag", g.fix.epx, g.fix.epv, g.satellites_used, g.dop.pdop, navmode, flags);
	
	printf("\r\n");
	
	{

	PGresult		   *res;
	int					nFields;
	int					i,
						j;
						
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


	
	sprintf(stmt, query1,
			postgres_seria_str, // Серия, φ, λ, h, 
			g.fix.latitude,
			g.fix.longitude,
			g.fix.altHAE,
			epx, epz, evx, evy, evz, //epx, epz, evx, evy, evz
			g.fix.speed, g.fix.climb, // , v, clm, d, t, epx1, epv, sptn, dop, mode
			t_d, t_h, t_m, t_s, t_ms,
			g.fix.epx, g.fix.epv,
			g.satellites_used, g.dop.pdop, navmode, flags	   	
		);
	printf("SQL --: ");
	printf(stmt);
	printf("\n");
		/* Отправка текста запроса в БД */
		res = PQexec(pg_conn,stmt);
		if(PQresultStatus(res) != PGRES_COMMAND_OK)
			fprintf(stderr, "%s", PQerrorMessage(pg_conn));
	}
}

static void ubx_update(void)
{
	unsigned char *buf;
	size_t data_len;
	unsigned short msgid;

	buf = session.lexer.outbuffer;
	// Тип присланного сообщения от U-Blox
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
}

static int ubx_command(char line[]UNUSED)
{
	return COMMAND_UNKNOWN;
}

const struct monitor_object_t ubx_mmt = {
	.update = ubx_update,
	.command = ubx_command,
	.driver = &driver_ubx,
};

// vim: set expandtab shiftwidth=4
