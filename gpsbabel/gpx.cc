/*
    Access GPX data files.

    Copyright (C) 2002-2013 Robert Lipe, gpsbabel.org

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA

 */

#include "defs.h"
#include "xmlgeneric.h"
#include "cet_util.h"
#include "garmin_fs.h"
#if HAVE_LIBEXPAT
#include <expat.h>
static XML_Parser psr;
#endif
#include "src/core/xmlstreamwriter.h"
#include <QtCore/QRegExp>
#include <QtCore/QDebug>
//#include <QtCore/QTextCodec>


static xml_tag* cur_tag;
static vmem_t cdatastr;
static char* opt_logpoint = NULL;
static char* opt_humminbirdext = NULL;
static char* opt_garminext = NULL;
static int logpoint_ct = 0;

static char* gpx_version = NULL;
static char* gpx_wversion;
static int gpx_wversion_num;
static const char* gpx_creator;
static char* xsi_schema_loc = NULL;

static char* gpx_email = NULL;
static char* gpx_author = NULL;
static vmem_t current_tag;

static waypoint* wpt_tmp;
static int cache_descr_is_html;
static gbfile* fd;
static const char* input_fname;
static gbfile* ofd = NULL;
static QString ostring;
static gpsbabel::XmlStreamWriter writer(ostring);
static short_handle mkshort_handle;
static const char* link_url;
static char* link_text;

static const char* input_string = NULL;
static int input_string_len = 0;

static time_t file_time;

static char* snlen = NULL;
static char* suppresswhite = NULL;
static char* urlbase = NULL;
static route_head* trk_head;
static route_head* rte_head;
static const route_head* current_trk_head;		// Output.
/* used for bounds calculation on output */
static bounds all_bounds;
static int next_trkpt_is_new_seg;

static format_specific_data** fs_ptr;


#define MYNAME "GPX"
#define MY_CBUF_SZ 4096
#define DEFAULT_XSI_SCHEMA_LOC "http://www.topografix.com/GPX/1/0 http://www.topografix.com/GPX/1/0/gpx.xsd"
#define DEFAULT_XSI_SCHEMA_LOC_11 "http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd"
#define DEFAULT_XSI_SCHEMA_LOC_FMT "\"http://www.topografix.com/GPX/%c/%c http://www.topografix.com/GPX/%c/%c/gpx.xsd\""
#ifndef CREATOR_NAME_URL
#  define CREATOR_NAME_URL "GPSBabel - http://www.gpsbabel.org"
#endif


/*
 * Format used for floating point formats.  Put in one place to make it
 * easier to tweak when comparing output with other GPX programs that
 * have more or less digits of output...
 */
/* #define FLT_FMT "%.9lf" */  /* ExpertGPS */
#define FLT_FMT "%0.9lf"
#define FLT_FMT_T "%0.9lf"
#define FLT_FMT_R "%0.9lf"

typedef enum {
  tt_unknown = 0,
  tt_gpx,

  tt_name,		/* Optional file-level info */
  tt_desc,
  tt_author,
  tt_email,
  tt_url,
  tt_urlname,
  tt_keywords,

  tt_wpt,
  tt_wpt_cmt,
  tt_wpt_desc,
  tt_wpt_name,
  tt_wpt_sym,
  tt_wpt_url,
  tt_wpt_ele,
  tt_wpt_time,
  tt_wpt_type,
  tt_wpt_urlname,
  tt_wpt_link, 		/* New in GPX 1.1 */
  tt_wpt_link_text, 	/* New in GPX 1.1 */
  tt_pdop,		/* PDOPS are common for all three */
  tt_hdop,		/* PDOPS are common for all three */
  tt_vdop,		/* PDOPS are common for all three */
  tt_fix,
  tt_sat,
  tt_cache,
  tt_cache_name,
  tt_cache_container,
  tt_cache_type,
  tt_cache_difficulty,
  tt_cache_terrain,
  tt_cache_hint,
  tt_cache_desc_short,
  tt_cache_desc_long,
  tt_cache_log_wpt,
  tt_cache_log_type,
  tt_cache_log_date,
  tt_cache_placer,
  tt_cache_favorite_points,
  tt_cache_personal_note,

  tt_wpt_extensions,

  tt_garmin_wpt_extensions,	/* don't change this order */
  tt_garmin_wpt_proximity,
  tt_garmin_wpt_temperature,
  tt_garmin_wpt_depth,
  tt_garmin_wpt_display_mode,
  tt_garmin_wpt_categories,
  tt_garmin_wpt_category,
  tt_garmin_wpt_addr,
  tt_garmin_wpt_city,
  tt_garmin_wpt_state,
  tt_garmin_wpt_country,
  tt_garmin_wpt_postal_code,
  tt_garmin_wpt_phone_nr,		/* don't change this order */

  tt_rte,
  tt_rte_name,
  tt_rte_desc,
  tt_rte_cmt,
  tt_rte_number,
  tt_rte_rtept,
  tt_rte_rtept_ele,
  tt_rte_rtept_name,
  tt_rte_rtept_desc,
  tt_rte_rtept_sym,
  tt_rte_rtept_time,
  tt_rte_rtept_cmt,
  tt_rte_rtept_url,
  tt_rte_rtept_urlname,
  tt_trk,
  tt_trk_desc,
  tt_trk_name,
  tt_trk_trkseg,
  tt_trk_number,
  tt_trk_trkseg_trkpt,
  tt_trk_trkseg_trkpt_cmt,
  tt_trk_trkseg_trkpt_name,
  tt_trk_trkseg_trkpt_sym,
  tt_trk_trkseg_trkpt_url,
  tt_trk_trkseg_trkpt_urlname,
  tt_trk_trkseg_trkpt_desc,
  tt_trk_trkseg_trkpt_ele,
  tt_trk_trkseg_trkpt_time,
  tt_trk_trkseg_trkpt_course,
  tt_trk_trkseg_trkpt_speed,
  tt_trk_trkseg_trkpt_heartrate,
  tt_trk_trkseg_trkpt_cadence,

  tt_humminbird_wpt_depth,
  tt_humminbird_wpt_status,
  tt_humminbird_trk_trkseg_trkpt_depth,
} tag_type;

typedef struct {
  struct queue queue;
  char* tagdata;
} gpx_global_entry;

/*
 * The file-level information.
 */
static
struct gpx_global {
  gpx_global_entry name;
  gpx_global_entry desc;
  gpx_global_entry author;
  gpx_global_entry email;
  gpx_global_entry url;
  gpx_global_entry urlname;
  gpx_global_entry keywords;
  /* time and bounds aren't here; they're recomputed. */
}* gpx_global ;

static void
gpx_add_to_global(gpx_global_entry* ge, char* cdata)
{
  queue* elem, *tmp;
  gpx_global_entry* gep;

  QUEUE_FOR_EACH(&ge->queue, elem, tmp) {
    gep = BASE_STRUCT(elem, gpx_global_entry, queue);
    if (0 == strcmp(cdata, gep->tagdata)) {
      return;
    }
  }

  gep = (gpx_global_entry*) xcalloc(sizeof(*gep), 1);
  QUEUE_INIT(&gep->queue);
  gep->tagdata = xstrdup(cdata);
  ENQUEUE_TAIL(&ge->queue, &gep->queue);
}

static void
gpx_rm_from_global(gpx_global_entry* ge)
{
  queue* elem, *tmp;

  QUEUE_FOR_EACH(&ge->queue, elem, tmp) {
    gpx_global_entry* g = (gpx_global_entry*) dequeue(elem);
    xfree(g->tagdata);
    xfree(g);
  }
}

/*
 * gpx_reset_short_handle: used for waypoint, route and track names
 * this allows gpx:wpt names to overlap gpx:rtept names, etc.
 */
static void
gpx_reset_short_handle(void)
{
  if (mkshort_handle != NULL) {
    mkshort_del_handle(&mkshort_handle);
  }

  mkshort_handle = mkshort_new_handle();

  if (suppresswhite) {
    setshort_whitespace_ok(mkshort_handle, 0);
  }

  setshort_length(mkshort_handle, atoi(snlen));
}

static void
gpx_write_gdata(gpx_global_entry* ge, const char* tag)
{
  queue* elem, *tmp;
  gpx_global_entry* gep;

  if (!gpx_global || QUEUE_EMPTY(&ge->queue)) {
    return;
  }
#if OLDGPX
  gbfprintf(ofd, "<%s>", tag);
#else
  writer.writeStartElement(tag);
#endif
  QUEUE_FOR_EACH(&ge->queue, elem, tmp) {
    gep = BASE_STRUCT(elem, gpx_global_entry, queue);
#if OLDGPX
    gbfprintf(ofd, "%s", gep->tagdata);
#else
    writer.writeCharacters(gep->tagdata);
#endif
    /* Some tags we just output once. */
    if ((0 == strcmp(tag, "url")) ||
        (0 == strcmp(tag, "email"))) {
      break;
    }
    gbfprintf(ofd, " ");
  }
#if OLDGPX
  gbfprintf(ofd, "</%s>\n", tag);
#else
  writer.writeEndElement();
#endif
}


typedef struct tag_mapping {
  tag_type tag_type_;		/* enum from above for this tag */
  int tag_passthrough;		/* true if we don't generate this */
  const char* tag_name;		/* xpath-ish tag name */
  unsigned long crc;		/* Crc32 of tag_name */
} tag_mapping;

/*
 * xpath(ish) mappings between full tag paths and internal identifers.
 * These appear in the order they appear in the GPX specification.
 * If it's not a tag we explictly handle, it doesn't go here.
 */

tag_mapping tag_path_map[] = {
  { tt_gpx, 0, "/gpx", 0UL },
  { tt_name, 0, "/gpx/name", 0UL },
  { tt_desc, 0, "/gpx/desc", 0UL },
  { tt_author, 0, "/gpx/author", 0UL },
  { tt_email, 0, "/gpx/email", 0UL },
  { tt_url, 0, "/gpx/url", 0UL },
  { tt_urlname, 0, "/gpx/urlname", 0UL },
  { tt_keywords, 0, "/gpx/keywords", 0UL },

  { tt_wpt, 0, "/gpx/wpt", 0UL },
  { tt_wpt_ele, 0, "/gpx/wpt/ele", 0UL },
  { tt_wpt_time, 0, "/gpx/wpt/time", 0UL },
  { tt_wpt_name, 0, "/gpx/wpt/name", 0UL },
  { tt_wpt_cmt, 0, "/gpx/wpt/cmt", 0UL },
  { tt_wpt_desc, 0, "/gpx/wpt/desc", 0UL },
  { tt_wpt_url, 0, "/gpx/wpt/url", 0UL },
  { tt_wpt_urlname, 0, "/gpx/wpt/urlname", 0UL },
  { tt_wpt_link, 0, "/gpx/wpt/link", 0UL },			/* GPX 1.1 */
  { tt_wpt_link_text, 0, "/gpx/wpt/link/text", 0UL },		/* GPX 1.1 */
  { tt_wpt_sym, 0, "/gpx/wpt/sym", 0UL },
  { tt_wpt_type, 1, "/gpx/wpt/type", 0UL },

  /* Double up the GPX 1.0 and GPX 1.1 styles */
#define GEOTAG(type,name) \
  {type, 1, "/gpx/wpt/groundspeak:cache/groundspeak:" name, 0UL }, \
  {type, 1, "/gpx/wpt/extensions/cache/" name, 0UL }, \
  {type, 1, "/gpx/wpt/geocache/" name, 0UL }  /* opencaching.de */

#define GARMIN_WPT_EXT "/gpx/wpt/extensions/gpxx:WaypointExtension"
#define GARMIN_TRK_EXT "/gpx/trk/trkseg/trkpt/extensions/gpxtpx:TrackPointExtension"
#define GARMIN_RTE_EXT "/gpx/rte/rtept/extensions/gpxxx:RoutePointExtension"

//	GEOTAG( tt_cache, 		"cache"),
  { tt_cache, 1, "/gpx/wpt/groundspeak:cache" },

  GEOTAG(tt_cache_name, 		"name"),
  GEOTAG(tt_cache_container, 	"container"),
  GEOTAG(tt_cache_type, 		"type"),
  GEOTAG(tt_cache_difficulty, 	"difficulty"),
  GEOTAG(tt_cache_terrain, 	"terrain"),
  GEOTAG(tt_cache_hint, 		"encoded_hints"),
  GEOTAG(tt_cache_hint, 		"hints"),  /* opencaching.de */
  GEOTAG(tt_cache_desc_short, 	"short_description"),
  GEOTAG(tt_cache_desc_long, 	"long_description"),
  GEOTAG(tt_cache_placer, 	"owner"),
  GEOTAG(tt_cache_favorite_points, 	"favorite_points"),
  GEOTAG(tt_cache_personal_note, 	"personal_note"),
  { tt_cache_log_wpt, 1, "/gpx/wpt/groundspeak:cache/groundspeak:logs/groundspeak:log/groundspeak:log_wpt"},
  { tt_cache_log_wpt, 1, "/gpx/wpt/extensions/cache/logs/log/log_wpt"},
  { tt_cache_log_type, 1, "/gpx/wpt/groundspeak:cache/groundspeak:logs/groundspeak:log/groundspeak:type"},
  { tt_cache_log_type, 1, "/gpx/wpt/extensions/cache/logs/log/type"},
  { tt_cache_log_date, 1, "/gpx/wpt/groundspeak:cache/groundspeak:logs/groundspeak:log/groundspeak:date"},
  { tt_cache_log_date, 1, "/gpx/wpt/extensions/cache/logs/log/date"},

  { tt_wpt_extensions, 0, "/gpx/wpt/extensions", 0UL },

  { tt_garmin_wpt_extensions, 0, GARMIN_WPT_EXT, 0UL },
  { tt_garmin_wpt_proximity, 0, GARMIN_WPT_EXT "/gpxx:Proximity", 0UL },
  { tt_garmin_wpt_temperature, 0, GARMIN_WPT_EXT "/gpxx:Temperature", 0UL },
  { tt_garmin_wpt_temperature, 0, GARMIN_TRK_EXT "/gpxtpx:atemp", 0UL },
  { tt_garmin_wpt_depth, 0, GARMIN_WPT_EXT "/gpxx:Depth", 0UL },
  { tt_garmin_wpt_display_mode, 0, GARMIN_WPT_EXT "/gpxx:DisplayMode", 0UL },
  { tt_garmin_wpt_categories, 0, GARMIN_WPT_EXT "/gpxx:Categories", 0UL },
  { tt_garmin_wpt_category, 0, GARMIN_WPT_EXT "/gpxx:Categories/gpxx:Category", 0UL },
  { tt_garmin_wpt_addr, 0, GARMIN_WPT_EXT "/gpxx:Address/gpxx:StreetAddress", 0UL },
  { tt_garmin_wpt_city, 0, GARMIN_WPT_EXT "/gpxx:Address/gpxx:City", 0UL },
  { tt_garmin_wpt_state, 0, GARMIN_WPT_EXT "/gpxx:Address/gpxx:State", 0UL },
  { tt_garmin_wpt_country, 0, GARMIN_WPT_EXT "/gpxx:Address/gpxx:Country", 0UL },
  { tt_garmin_wpt_postal_code, 0, GARMIN_WPT_EXT "/gpxx:Address/gpxx:PostalCode", 0UL },
  { tt_garmin_wpt_phone_nr, 0, GARMIN_WPT_EXT "/gpxx:PhoneNumber", 0UL },

  // In Garmin space, but in core of waypoint.
  { tt_trk_trkseg_trkpt_heartrate, 0, GARMIN_TRK_EXT "/gpxtpx:hr", 0UL },
  { tt_trk_trkseg_trkpt_cadence, 0, GARMIN_TRK_EXT "/gpxtpx:cad", 0UL },

  { tt_humminbird_wpt_depth, 0, "/gpx/wpt/extensions/h:depth", 0UL },  // in centimeters.
  { tt_humminbird_wpt_status, 0, "/gpx/wpt/extensions/h:status", 0UL },

  { tt_rte, 0, "/gpx/rte", 0UL },
  { tt_rte_name, 0, "/gpx/rte/name", 0UL },
  { tt_rte_desc, 0, "/gpx/rte/desc", 0UL },
  { tt_rte_number, 0, "/gpx/rte/number", 0UL },
  { tt_rte_rtept, 0, "/gpx/rte/rtept", 0UL },
  { tt_rte_rtept_ele, 0, "/gpx/rte/rtept/ele", 0UL },
  { tt_rte_rtept_time, 0, "/gpx/rte/rtept/time", 0UL },
  { tt_rte_rtept_name, 0, "/gpx/rte/rtept/name", 0UL },
  { tt_rte_rtept_cmt, 0, "/gpx/rte/rtept/cmt", 0UL },
  { tt_rte_rtept_desc, 0, "/gpx/rte/rtept/desc", 0UL },
  { tt_rte_rtept_url, 0, "/gpx/rte/rtept/url", 0UL },
  { tt_rte_rtept_urlname, 0, "/gpx/rte/rtept/urlname", 0UL },
  { tt_rte_rtept_sym, 0, "/gpx/rte/rtept/sym", 0UL },

  { tt_trk, 0, "/gpx/trk", 0UL },
  { tt_trk_name, 0, "/gpx/trk/name", 0UL },
  { tt_trk_desc, 0, "/gpx/trk/desc", 0UL },
  { tt_trk_trkseg, 0, "/gpx/trk/trkseg", 0UL },
  { tt_trk_number, 0, "/gpx/trk/number", 0UL },
  { tt_trk_trkseg_trkpt, 0, "/gpx/trk/trkseg/trkpt", 0UL },
  { tt_trk_trkseg_trkpt_ele, 0, "/gpx/trk/trkseg/trkpt/ele", 0UL },
  { tt_trk_trkseg_trkpt_time, 0, "/gpx/trk/trkseg/trkpt/time", 0UL },
  { tt_trk_trkseg_trkpt_name, 0, "/gpx/trk/trkseg/trkpt/name", 0UL },
  { tt_trk_trkseg_trkpt_cmt, 0, "/gpx/trk/trkseg/trkpt/cmt", 0UL },
  { tt_trk_trkseg_trkpt_desc, 0, "/gpx/trk/trkseg/trkpt/desc", 0UL },
  { tt_trk_trkseg_trkpt_url, 0, "/gpx/trk/trkseg/trkpt/url", 0UL },
  { tt_trk_trkseg_trkpt_urlname, 0, "/gpx/trk/trkseg/trkpt/urlname", 0UL },
  { tt_trk_trkseg_trkpt_sym, 0, "/gpx/trk/trkseg/trkpt/sym", 0UL },
  { tt_trk_trkseg_trkpt_course, 0, "/gpx/trk/trkseg/trkpt/course", 0UL },
  { tt_trk_trkseg_trkpt_speed, 0, "/gpx/trk/trkseg/trkpt/speed", 0UL },

  { tt_humminbird_trk_trkseg_trkpt_depth, 0, "/gpx/trk/trkseg/trkpt/extensions/h:depth", 0UL },  // in centimeters.

  /* Common to tracks, routes, and waypts */
  { tt_fix,  0, "/gpx/wpt/fix", 0UL },
  { tt_fix,  0, "/gpx/trk/trkseg/trkpt/fix", 0UL },
  { tt_fix,  0, "/gpx/rte/rtept/fix", 0UL },
  { tt_sat,  0, "/gpx/wpt/sat", 0UL },
  { tt_sat,  0, "/gpx/trk/trkseg/trkpt/sat", 0UL },
  { tt_sat,  0, "/gpx/rte/rtept/sat", 0UL },
  { tt_pdop, 0, "/gpx/wpt/pdop", 0UL },
  { tt_pdop, 0, "/gpx/trk/trkseg/trkpt/pdop", 0UL },
  { tt_pdop, 0, "/gpx/rte/rtept/pdop", 0UL },
  { tt_hdop, 0, "/gpx/wpt/hdop", 0UL },
  { tt_hdop, 0, "/gpx/trk/trkseg/trkpt/hdop", 0UL },
  { tt_hdop, 0, "/gpx/rte/rtept/hdop", 0UL },
  { tt_vdop, 0, "/gpx/wpt/vdop", 0UL },
  { tt_vdop, 0, "/gpx/trk/trkseg/trkpt/vdop", 0UL },
  { tt_vdop, 0, "/gpx/rte/rtept/hdop", 0UL },
  {(tag_type)0, 0, NULL, 0UL}
};

static tag_type
get_tag(const char* t, int* passthrough)
{
  tag_mapping* tm;
  unsigned long tcrc = get_crc32_s(t);

  for (tm = tag_path_map; tm->tag_type_ != 0; tm++) {
    if ((tcrc == tm->crc) && (0 == strcmp(tm->tag_name, t))) {
      *passthrough = tm->tag_passthrough;
      return tm->tag_type_;
    }
  }
  *passthrough = 1;
  return tt_unknown;
}

static void
prescan_tags(void)
{
  tag_mapping* tm;
  for (tm = tag_path_map; tm->tag_type_ != 0; tm++) {
    tm->crc = get_crc32_s(tm->tag_name);
  }
}

static void
tag_gpx(const char** attrv)
{
  const char** avp;
  for (avp = &attrv[0]; *avp; avp += 2) {
    if (strcmp(avp[0], "version") == 0) {
      /* Set the default output version to the highest input
       * version.
       */
      if (! gpx_version) {
        gpx_version = xstrdup(avp[1]);
      } else if ((strtod(gpx_version, NULL) * 10) < (strtod(avp[1], NULL) * 10)) {
        xfree(gpx_version);
        gpx_version = xstrdup(avp[1]);
      }
    } else if (strcmp(avp[0], "src") == 0) {
      gpx_creator = avp[1];
    }
    /*
     * Our handling of schemaLocation really is weird.
     * If we see we have a "normal" GPX 1.1 header, on read,
     * flip our default on write to use that and don't append
     * it to the rest...
     */
    else if (strcmp(avp[0], "xsi:schemaLocation") == 0) {
      if (0 == strcmp(avp[1], DEFAULT_XSI_SCHEMA_LOC_11)) {
        if (0 == strcmp(xsi_schema_loc, DEFAULT_XSI_SCHEMA_LOC)) {
          xfree(xsi_schema_loc);
          xsi_schema_loc = xstrdup(DEFAULT_XSI_SCHEMA_LOC_11);
        }
        continue;
      }
      if (0 == strstr(xsi_schema_loc, avp[1])) {
        xsi_schema_loc = xstrappend(xsi_schema_loc, " ");
        xsi_schema_loc = xstrappend(xsi_schema_loc, avp[1]);
      }
    }
  }
}

static void
tag_wpt(const char** attrv)
{
  const char** avp = &attrv[0];

  wpt_tmp = waypt_new();

  cur_tag = NULL;
  while (*avp) {
    if (strcmp(avp[0], "lat") == 0) {
      sscanf(avp[1], "%lf",
             &wpt_tmp->latitude);
    } else if (strcmp(avp[0], "lon") == 0) {
      sscanf(avp[1], "%lf",
             &wpt_tmp->longitude);
    }
    avp+=2;
  }
  fs_ptr = &wpt_tmp->fs;
}

static void
tag_cache_desc(const char** attrv)
{
  const char** avp;

  cache_descr_is_html = 0;
  for (avp = &attrv[0]; *avp; avp+=2) {
    if (strcmp(avp[0], "html") == 0) {
      if (strcmp(avp[1], "True") == 0) {
        cache_descr_is_html = 1;
      }
    }
  }
}

static void
tag_gs_cache(const char** attrv)
{
  const char** avp;
  geocache_data* gc_data = waypt_alloc_gc_data(wpt_tmp);

  for (avp = &attrv[0]; *avp; avp+=2) {
    if (strcmp(avp[0], "id") == 0) {
      gc_data->id = atoi(avp[1]);
    } else if (strcmp(avp[0], "available") == 0) {
      if (case_ignore_strcmp(avp[1], "True") == 0) {
        gc_data->is_available = status_true;
      } else if (case_ignore_strcmp(avp[1], "False") == 0) {
        gc_data->is_available = status_false;
      }
    } else if (strcmp(avp[0], "archived") == 0) {
      if (case_ignore_strcmp(avp[1], "True") == 0) {
        gc_data->is_archived = status_true;
      } else if (case_ignore_strcmp(avp[1], "False") == 0) {
        gc_data->is_archived = status_false;
      }
    }
  }
}

static void
start_something_else(const char* el, const char** attrv)
{
  const char** avp = attrv;
  char** avcp = NULL;
  int attr_count = 0;
  xml_tag* new_tag;
  fs_xml* fs_gpx;

  if (!fs_ptr) {
    return;
  }

  new_tag = (xml_tag*)xcalloc(sizeof(xml_tag),1);
  new_tag->tagname = xstrdup(el);

  /* count attributes */
  while (*avp) {
    attr_count++;
    avp++;
  }

  /* copy attributes */
  avp = attrv;
  new_tag->attributes = (char**)xcalloc(sizeof(char*),attr_count+1);
  avcp = new_tag->attributes;
  while (*avp) {
    *avcp = xstrdup(*avp);
    avcp++;
    avp++;
  }
  *avcp = NULL;

  if (cur_tag) {
    if (cur_tag->child) {
      cur_tag = cur_tag->child;
      while (cur_tag->sibling) {
        cur_tag = cur_tag->sibling;
      }
      cur_tag->sibling = new_tag;
      new_tag->parent = cur_tag->parent;
    } else {
      cur_tag->child = new_tag;
      new_tag->parent = cur_tag;
    }
  } else {
    fs_gpx = (fs_xml*)fs_chain_find(*fs_ptr, FS_GPX);

    if (fs_gpx && fs_gpx->tag) {
      cur_tag = fs_gpx->tag;
      while (cur_tag->sibling) {
        cur_tag = cur_tag->sibling;
      }
      cur_tag->sibling = new_tag;
      new_tag->parent = NULL;
    } else {
      fs_gpx = fs_xml_alloc(FS_GPX);
      fs_gpx->tag = new_tag;
      fs_chain_add(fs_ptr, (format_specific_data*)fs_gpx);
      new_tag->parent = NULL;
    }
  }
  cur_tag = new_tag;
}

static void
end_something_else()
{
  if (cur_tag) {
    cur_tag = cur_tag->parent;
  }
}

static void
tag_log_wpt(const char** attrv)
{
  waypoint* lwp_tmp;
  const char** avp = &attrv[0];

  /* create a new waypoint */
  lwp_tmp = waypt_new();

  /* extract the lat/lon attributes */
  while (*avp) {
    if (strcmp(avp[0], "lat") == 0) {
      sscanf(avp[1], "%lf",
             &lwp_tmp->latitude);
    } else if (strcmp(avp[0], "lon") == 0) {
      sscanf(avp[1], "%lf",
             &lwp_tmp->longitude);
    }
    avp+=2;
  }
  /* Make a new shortname.  Since this is a groundspeak extension,
    we assume that GCBLAH is the current shortname format and that
    wpt_tmp refers to the currently parsed waypoint. Unfortunatley,
    we need to keep track of log_wpt counts so we don't collide with
    dupe shortnames.
  */

  if ((wpt_tmp->shortname) && (strlen(wpt_tmp->shortname) > 2)) {
    /* copy of the shortname */
    lwp_tmp->shortname = (char*) xcalloc(7, 1);
    sprintf(lwp_tmp->shortname, "%-4.4s%02d",
            &wpt_tmp->shortname[2], logpoint_ct++);

    waypt_add(lwp_tmp);
  }
}

static void
gpx_start(void* data, const XML_Char* xml_el, const XML_Char** xml_attr)
{
  char* e;
  char* ep;
  int passthrough;
  int tag;
  const char* el = xml_convert_to_char_string(xml_el);
  const char** attr = xml_convert_attrs_to_char_string(xml_attr);

  vmem_realloc(&current_tag, strlen(current_tag.mem) + 2 + strlen(el));
  e = current_tag.mem;
  ep = e + strlen(e);
  *ep++ = '/';
  strcpy(ep, el);


  /*
   * Reset end-of-string without actually emptying/reallocing cdatastr.
   */
  *(char*) cdatastr.mem = 0;

  tag = get_tag(current_tag.mem, &passthrough);
  switch (tag) {
  case tt_gpx:
    tag_gpx(attr);
    break;
  case tt_wpt:
    tag_wpt(attr);
    break;
  case tt_wpt_link:
    if (attr[0] && attr[1] && 0 == strcmp(attr[0], "href")) {
      link_url = attr[1];
    }
    break;
  case tt_wpt_link_text:
    link_text = cdatastr.mem;
    break;
  case tt_rte:
    rte_head = route_head_alloc();
    route_add_head(rte_head);
    fs_ptr = &rte_head->fs;
    break;
  case tt_rte_rtept:
    tag_wpt(attr);
    break;
  case tt_trk:
    trk_head = route_head_alloc();
    track_add_head(trk_head);
    fs_ptr = &trk_head->fs;
    break;
  case tt_trk_trkseg_trkpt:
    tag_wpt(attr);
    if (next_trkpt_is_new_seg) {
      wpt_tmp->wpt_flags.new_trkseg = 1;
      next_trkpt_is_new_seg = 0;
    }
    break;
  case tt_unknown:
    start_something_else(el, attr);
    return;
  case tt_cache:
    tag_gs_cache(attr);
    break;
  case tt_cache_log_wpt:
    if (opt_logpoint) {
      tag_log_wpt(attr);
    }
    break;
  case tt_cache_desc_long:
  case tt_cache_desc_short:
    tag_cache_desc(attr);
    break;
  case tt_cache_placer:
    if (*attr && (0 == strcmp(attr[0], "id"))) {
      waypt_alloc_gc_data(wpt_tmp)->placer_id = atoi(attr[1]);
    }
  default:
    break;
  }
  if (passthrough) {
    start_something_else(el, attr);
  }
  xml_free_converted_string(el);
  xml_free_converted_attrs(attr);
}

struct
    gs_type_mapping {
  geocache_type type;
  const char* name;
} gs_type_map[] = {
  { gt_traditional, "Traditional Cache" },
  { gt_traditional, "Traditional" }, /* opencaching.de */
  { gt_multi, "Multi-cache" },
  { gt_multi, "Multi" }, /* opencaching.de */
  { gt_virtual, "Virtual Cache" },
  { gt_virtual, "Virtual" }, /* opencaching.de */
  { gt_event, "Event Cache" },
  { gt_event, "Event" }, /* opencaching.de */
  { gt_webcam, "Webcam Cache" },
  { gt_webcam, "Webcam" }, /* opencaching.de */
  { gt_suprise, "Unknown Cache" },
  { gt_earth, "Earthcache" },
  { gt_earth, "Earth" }, /* opencaching.de */
  { gt_cito, "Cache In Trash Out Event" },
  { gt_letterbox, "Letterbox Hybrid" },
  { gt_locationless, "Locationless (Reverse) Cache" },
  { gt_ape, "Project APE Cache" },
  { gt_mega, "Mega-Event Cache" },
  { gt_wherigo, "Wherigo Cache" },

  { gt_benchmark, "Benchmark" }, /* Not Groundspeak; for GSAK  */
};

struct
    gs_container_mapping {
  geocache_container type;
  const char* name;
} gs_container_map[] = {
  { gc_other, "Unknown" },
  { gc_other, "Other" }, /* Synonym on read. */
  { gc_micro, "Micro" },
  { gc_regular, "Regular" },
  { gc_large, "Large" },
  { gc_small, "Small" },
  { gc_virtual, "Virtual" }
};

geocache_type
gs_mktype(const char* t)
{
  int i;
  int sz = sizeof(gs_type_map) / sizeof(gs_type_map[0]);

  for (i = 0; i < sz; i++) {
    if (0 == case_ignore_strcmp(t, gs_type_map[i].name)) {
      return gs_type_map[i].type;
    }
  }
  return gt_unknown;
}

const char*
gs_get_cachetype(geocache_type t)
{
  int i;
  int sz = sizeof(gs_type_map) / sizeof(gs_type_map[0]);

  for (i = 0; i < sz; i++) {
    if (t == gs_type_map[i].type) {
      return gs_type_map[i].name;
    }
  }
  return "Unknown";
}

geocache_container
gs_mkcont(const char* t)
{
  int i;
  int sz = sizeof(gs_container_map) / sizeof(gs_container_map[0]);

  for (i = 0; i < sz; i++) {
    if (0 == case_ignore_strcmp(t, gs_container_map[i].name)) {
      return gs_container_map[i].type;
    }
  }
  return gc_unknown;
}

const char*
gs_get_container(geocache_container t)
{
  int i;
  int sz = sizeof(gs_container_map) / sizeof(gs_container_map[0]);

  for (i = 0; i < sz; i++) {
    if (t == gs_container_map[i].type) {
      return gs_container_map[i].name;
    }
  }
  return "Unknown";
}

time_t
xml_parse_time(const char* cdatastr, int* microsecs)
{
  int off_hr = 0;
  int off_min = 0;
  int off_sign = 1;
  char* offsetstr = NULL;
  char* pointstr = NULL;
  struct tm tm;
  time_t rv = 0;
  char* timestr = xstrdup(cdatastr);

  memset(&tm, 0, sizeof(tm));

  offsetstr = strchr(timestr, 'Z');
  if (offsetstr) {
    /* zulu time; offsets stay at defaults */
    *offsetstr = '\0';
  } else {
    offsetstr = strchr(timestr, '+');
    if (offsetstr) {
      /* positive offset; parse it */
      *offsetstr = '\0';
      sscanf(offsetstr+1, "%d:%d", &off_hr, &off_min);
    } else {
      offsetstr = strchr(timestr, 'T');
      if (offsetstr) {
        offsetstr = strchr(offsetstr, '-');
        if (offsetstr) {
          /* negative offset; parse it */
          *offsetstr = '\0';
          sscanf(offsetstr+1, "%d:%d",
                 &off_hr, &off_min);
          off_sign = -1;
        }
      }
    }
  }

  pointstr = strchr(timestr, '.');
  if (pointstr) {
    if (microsecs) {
      double fsec;
      sscanf(pointstr, "%le", &fsec);
      /* Round to avoid FP jitter */
      *microsecs = .5 + (fsec * 1000000.0) ;
    }
    *pointstr = '\0';
  }

  sscanf(timestr, "%d-%d-%dT%d:%d:%d",
         &tm.tm_year,
         &tm.tm_mon,
         &tm.tm_mday,
         &tm.tm_hour,
         &tm.tm_min,
         &tm.tm_sec);
  tm.tm_mon -= 1;
  tm.tm_year -= 1900;
  tm.tm_isdst = 0;

  rv = mkgmtime(&tm) - off_sign*off_hr*3600 - off_sign*off_min*60;

  xfree(timestr);

  return rv;
}

static void
gpx_end(void* data, const XML_Char* xml_el)
{
  const char* el = xml_convert_to_char_string(xml_el);
  char* s = strrchr(current_tag.mem, '/');
  float x;
  char* cdatastrp = cdatastr.mem;
  int passthrough;
  static time_t gc_log_date;
  tag_type tag;

  if (strcmp(s + 1, el)) {
    fprintf(stderr, "Mismatched tag %s\n", el);
  }

  tag = get_tag(current_tag.mem, &passthrough);
  switch (tag) {
    /*
     * First, the tags that are file-global.
     */
  case tt_name:
    gpx_add_to_global(&gpx_global->name, cdatastrp);
    break;
  case tt_desc:
    gpx_add_to_global(&gpx_global->desc, cdatastrp);
    break;
  case tt_author:
    gpx_add_to_global(&gpx_global->author, cdatastrp);
    break;
  case tt_email:
    gpx_add_to_global(&gpx_global->email, cdatastrp);
    if (gpx_email == NULL) {
      gpx_email = xstrdup(cdatastrp);
    }
    break;
  case tt_url:
    gpx_add_to_global(&gpx_global->url, cdatastrp);
    break;
  case tt_urlname:
    gpx_add_to_global(&gpx_global->urlname, cdatastrp);
    break;
  case tt_keywords:
    gpx_add_to_global(&gpx_global->keywords, cdatastrp);
    break;

    /*
     * Waypoint-specific tags.
     */
  case tt_wpt:
    waypt_add(wpt_tmp);
    logpoint_ct = 0;
    cur_tag = NULL;
    wpt_tmp = NULL;
    break;
  case tt_cache_name:
    if (wpt_tmp->notes != NULL) {
      xfree(wpt_tmp->notes);
    }
    wpt_tmp->notes = xstrdup(cdatastrp);
    break;
  case tt_cache_container:
    waypt_alloc_gc_data(wpt_tmp)->container = gs_mkcont(cdatastrp);
    break;
  case tt_cache_type:
    waypt_alloc_gc_data(wpt_tmp)->type = gs_mktype(cdatastrp);
    break;
  case tt_cache_difficulty:
    sscanf(cdatastrp, "%f", &x);
    waypt_alloc_gc_data(wpt_tmp)->diff = x * 10;
    break;
  case tt_cache_hint:
    rtrim(cdatastrp);
    if (cdatastrp[0]) {
      waypt_alloc_gc_data(wpt_tmp)->hint = xstrdup(cdatastrp);
    }
    break;
  case tt_cache_desc_long:
    rtrim(cdatastrp);
    if (cdatastrp[0]) {
      geocache_data* gc_data = waypt_alloc_gc_data(wpt_tmp);
      gc_data->desc_long.is_html = cache_descr_is_html;
      gc_data->desc_long.utfstring = xstrdup(cdatastrp);
    }
    break;
  case tt_cache_desc_short:
    rtrim(cdatastrp);
    if (cdatastrp[0]) {
      geocache_data* gc_data = waypt_alloc_gc_data(wpt_tmp);
      gc_data->desc_short.is_html = cache_descr_is_html;
      gc_data->desc_short.utfstring = xstrdup(cdatastrp);
    }
    break;
  case tt_cache_terrain:
    sscanf(cdatastrp, "%f", &x);
    waypt_alloc_gc_data(wpt_tmp)->terr = x * 10;
    break;
  case tt_cache_placer:
    waypt_alloc_gc_data(wpt_tmp)->placer = xstrdup(cdatastrp);
    break;
  case tt_cache_log_date:
    gc_log_date = xml_parse_time(cdatastrp, NULL);
    break;
    /*
     * "Found it" logs follow the date according to the schema,
     * if this is the first "found it" for this waypt, just use the
     * last date we saw in this log.
     */
  case tt_cache_log_type:
    if ((0 == strcmp(cdatastrp, "Found it")) &&
        (0 == wpt_tmp->gc_data->last_found)) {
      waypt_alloc_gc_data(wpt_tmp)->last_found  = gc_log_date;
    }
    gc_log_date = 0;
    break;
  case tt_cache_favorite_points:
    waypt_alloc_gc_data(wpt_tmp)->favorite_points  = atoi(cdatastrp);
    break;
  case tt_cache_personal_note:
    waypt_alloc_gc_data(wpt_tmp)->personal_note  = xstrdup(cdatastrp);
    break;

    /*
     * Garmin-waypoint-specific tags.
     */
  case tt_garmin_wpt_proximity:
  case tt_garmin_wpt_temperature:
  case tt_garmin_wpt_depth:
  case tt_garmin_wpt_display_mode:
  case tt_garmin_wpt_category:
  case tt_garmin_wpt_addr:
  case tt_garmin_wpt_city:
  case tt_garmin_wpt_state:
  case tt_garmin_wpt_country:
  case tt_garmin_wpt_postal_code:
  case tt_garmin_wpt_phone_nr:
    garmin_fs_xml_convert(tt_garmin_wpt_extensions, tag, cdatastrp, wpt_tmp);
    break;

    /*
     * Humminbird-waypoint-specific tags.
     */
  case tt_humminbird_wpt_depth:
  case tt_humminbird_trk_trkseg_trkpt_depth:
    WAYPT_SET(wpt_tmp, depth, atof(cdatastrp) / 100.0)
    break;
    /*
     * Route-specific tags.
     */
  case tt_rte_name:
    rte_head->rte_name = xstrdup(cdatastrp);
    break;
  case tt_rte:
    break;
  case tt_rte_rtept:
    route_add_wpt(rte_head, wpt_tmp);
    wpt_tmp = NULL;
    break;
  case tt_rte_desc:
    rte_head->rte_desc = xstrdup(cdatastrp);
    break;
  case tt_rte_number:
    rte_head->rte_num = atoi(cdatastrp);
    break;
    /*
     * Track-specific tags.
     */
  case tt_trk_name:
    trk_head->rte_name = xstrdup(cdatastrp);
    break;
  case tt_trk:
    break;
  case tt_trk_trkseg:
    next_trkpt_is_new_seg = 1;
    break;
  case tt_trk_trkseg_trkpt:
    track_add_wpt(trk_head, wpt_tmp);
    wpt_tmp = NULL;
    break;
  case tt_trk_desc:
    trk_head->rte_desc = xstrdup(cdatastrp);
    break;
  case tt_trk_number:
    trk_head->rte_num = atoi(cdatastrp);
    break;
  case tt_trk_trkseg_trkpt_course:
    WAYPT_SET(wpt_tmp, course, atof(cdatastrp));
    break;
  case tt_trk_trkseg_trkpt_speed:
    WAYPT_SET(wpt_tmp, speed, atof(cdatastrp));
    break;
  case tt_trk_trkseg_trkpt_heartrate:
    wpt_tmp->heartrate = atof(cdatastrp);
    break;
  case tt_trk_trkseg_trkpt_cadence:
    wpt_tmp->cadence = atof(cdatastrp);
    break;

    /*
     * Items that are actually in multiple categories.
     */
  case tt_wpt_ele:
  case tt_rte_rtept_ele:
  case tt_trk_trkseg_trkpt_ele:
    sscanf(cdatastrp, "%lf", &wpt_tmp->altitude);
    break;
  case tt_wpt_name:
  case tt_rte_rtept_name:
  case tt_trk_trkseg_trkpt_name:
    wpt_tmp->shortname = xstrdup(cdatastrp);
    break;
  case tt_wpt_sym:
  case tt_rte_rtept_sym:
  case tt_trk_trkseg_trkpt_sym:
    wpt_tmp->icon_descr = cdatastrp;
    break;
  case tt_wpt_time:
  case tt_trk_trkseg_trkpt_time:
  case tt_rte_rtept_time:
    wpt_tmp->creation_time = xml_parse_time(cdatastrp, &wpt_tmp->microseconds);
    break;
  case tt_wpt_cmt:
  case tt_rte_rtept_cmt:
  case tt_trk_trkseg_trkpt_cmt:
    wpt_tmp->description = xstrdup(cdatastrp);
    break;
  case tt_wpt_desc:
  case tt_trk_trkseg_trkpt_desc:
  case tt_rte_rtept_desc:
    if (wpt_tmp->notes != NULL) {
      xfree(wpt_tmp->notes);
    }
    wpt_tmp->notes = xstrdup(cdatastrp);
    break;
  case tt_pdop:
    wpt_tmp->pdop = atof(cdatastrp);
    break;
  case tt_hdop:
    wpt_tmp->hdop = atof(cdatastrp);
    break;
  case tt_vdop:
    wpt_tmp->vdop = atof(cdatastrp);
    break;
  case tt_sat:
    wpt_tmp->sat = atof(cdatastrp);
    break;
  case tt_fix:
    wpt_tmp->fix = (fix_type)(atoi(cdatastrp)-1);
    if (wpt_tmp->fix < fix_2d) {
      if (!case_ignore_strcmp(cdatastrp, "none")) {
        wpt_tmp->fix = fix_none;
      } else if (!case_ignore_strcmp(cdatastrp, "dgps")) {
        wpt_tmp->fix = fix_dgps;
      } else if (!case_ignore_strcmp(cdatastrp, "pps")) {
        wpt_tmp->fix = fix_pps;
      } else {
        wpt_tmp->fix = fix_unknown;
      }
    }
    break;
  case tt_wpt_url:
  case tt_trk_trkseg_trkpt_url:
  case tt_rte_rtept_url:
    wpt_tmp->url = xstrdup(cdatastrp);
    break;
  case tt_wpt_urlname:
  case tt_trk_trkseg_trkpt_urlname:
  case tt_rte_rtept_urlname:
    wpt_tmp->url_link_text = xstrdup(cdatastrp);
    break;
  case tt_wpt_link:
//TODO: implement GPX 1.1 	case tt_trk_trkseg_trkpt_link:
//TODO: implement GPX 1.1 	case tt_rte_rtept_link:
  {
    char* lt = link_text;
    if (lt) {
      lt = xstrdup(lrtrim(link_text));
    }

    waypt_add_url(wpt_tmp, xstrdup(link_url), lt);
    link_text = NULL;
  }
  break;
  case tt_unknown:
    end_something_else();
    *s = 0;
    return;
  default:
    break;
  }

  if (passthrough) {
    end_something_else();
  }

  *s = 0;
  xml_free_converted_string(el);
}

#if ! HAVE_LIBEXPAT
static void
gpx_rd_init(const char* fname)
{
  fatal(MYNAME ": This build excluded GPX support because expat was not installed.\n");
}

static void
gpx_rd_deinit(void)
{
}

#else /* NO_EXPAT */

static void
gpx_cdata(void* dta, const XML_Char* xml_el, int len)
{
  char* estr;
  int* cdatalen;
  char** cdata;
  xml_tag* tmp_tag;
  size_t slen = strlen(cdatastr.mem);
  const char* s = xml_convert_to_char_string_n(xml_el, &len);

  vmem_realloc(&cdatastr,  1 + len + slen);
  estr = ((char*) cdatastr.mem) + slen;
  memcpy(estr, s, len);
  estr[len]  = 0;

  if (!cur_tag) {
    return;
  }

  if (cur_tag->child) {
    tmp_tag = cur_tag->child;
    while (tmp_tag->sibling) {
      tmp_tag = tmp_tag->sibling;
    }
    cdata = &(tmp_tag->parentcdata);
    cdatalen = &(tmp_tag->parentcdatalen);
  } else {
    cdata = &(cur_tag->cdata);
    cdatalen = &(cur_tag->cdatalen);
  }
  estr = *cdata;
  *cdata = (char*) xrealloc(*cdata, *cdatalen + len + 1);
  estr = *cdata + *cdatalen;
  memcpy(estr, s, len);
  *(estr+len) = '\0';
  *cdatalen += len;

  xml_free_converted_string(s);
}

static void
gpx_rd_init(const char* fname)
{
  if (fname[0]) {
    fd = gbfopen(fname, "r", MYNAME);
    input_fname = fname;
  } else {
    fd = NULL;
    input_string = fname+1;
    input_string_len = strlen(input_string);
    input_fname = NULL;
  }


  file_time = 0;
  current_tag = vmem_alloc(1, 0);
  *((char*)current_tag.mem) = '\0';

  prescan_tags();

  psr = XML_ParserCreate(NULL);
  if (!psr) {
    fatal(MYNAME ": Cannot create XML Parser\n");
  }
  XML_SetUnknownEncodingHandler(psr, cet_lib_expat_UnknownEncodingHandler, NULL);

  cdatastr = vmem_alloc(1, 0);
  *((char*)cdatastr.mem) = '\0';

  if (!xsi_schema_loc) {
    xsi_schema_loc = xstrdup(DEFAULT_XSI_SCHEMA_LOC);
  }
  if (!xsi_schema_loc) {
    fatal("gpx: Unable to allocate %ld bytes of memory.\n",
          (unsigned long) strlen(DEFAULT_XSI_SCHEMA_LOC) + 1);
  }

  if (NULL == gpx_global) {
    gpx_global = (struct gpx_global*) xcalloc(sizeof(*gpx_global), 1);
    QUEUE_INIT(&gpx_global->name.queue);
    QUEUE_INIT(&gpx_global->desc.queue);
    QUEUE_INIT(&gpx_global->author.queue);
    QUEUE_INIT(&gpx_global->email.queue);
    QUEUE_INIT(&gpx_global->url.queue);
    QUEUE_INIT(&gpx_global->urlname.queue);
    QUEUE_INIT(&gpx_global->keywords.queue);
  }

  XML_SetElementHandler(psr, gpx_start, gpx_end);
  XML_SetCharacterDataHandler(psr, gpx_cdata);
  fs_ptr = NULL;
}

static
void
gpx_rd_deinit(void)
{
  vmem_free(&current_tag);
  vmem_free(&cdatastr);
  /*
   * Don't free schema_loc.  It really is important that we preserve
   * this across reads or else merges/copies of files with different
   * schemas won't retain the headers.
   *
   *  moved to gpx_exit

  if ( xsi_schema_loc ) {
  	xfree(xsi_schema_loc);
  	xsi_schema_loc = NULL;
  }
  */

  if (gpx_email) {
    xfree(gpx_email);
    gpx_email = NULL;
  }
  if (gpx_author) {
    xfree(gpx_author);
    gpx_author = NULL;
  }
  if (fd) {
    gbfclose(fd);
  }
  XML_ParserFree(psr);
  psr = NULL;
  wpt_tmp = NULL;
  cur_tag = NULL;
  input_fname = NULL;
}
#endif

static void
gpx_wr_init(const char* fname)
{
  mkshort_handle = NULL;
  ofd = gbfopen(fname, "w", MYNAME);

  writer.setAutoFormattingIndent(2);
//  writer.setCodec("UTF-8");
//  writer.writeStartDocument();
  writer.writeProcessingInstruction("xml","version=\"1.0\" encoding=\"UTF-8\"");

}

static void
gpx_wr_deinit(void)
{
#if OLDGPX
  gbfclose(ofd);
#else
  writer.writeEndDocument();
#define XDEBUG 0
#if XDEBUG
  qDebug() << ostring;
#else
  // TODO: technically, &apos is almost never required to be encoded, but 
  // the pre-Qt version of our serializer did, so we'll manually do it here
  // for now, in the early days, just to keep things as we've always done it.
  ostring.replace("'", "&apos;");  

  // TODO: The old writer would more aggressively protect you from control
  // character nonsense.  The control-Z (032)is the only thing that appears in 
  // our test suite, but let's toss things we know aren't allowed in GPX.
  // Let's just carpet-bomb the whole range for now.  I have a feeling we'll
  // revisit this in time...
  ostring.replace(QRegExp("[\014-\032]"), " ");  

  gbfputs(ostring, ofd);
  gbfclose(ofd);
  ofd = NULL;
#endif
#endif
  
  mkshort_del_handle(&mkshort_handle);
}

void
gpx_read(void)
{
#if HAVE_LIBEXPAT
  int len;
  int done = 0;
  char* buf = (char*) xmalloc(MY_CBUF_SZ);
  int result = 0;
  int extra;

  while (!done) {
    if (fd) {
      /*
       * The majority of this block (in fact, all but the
       * call to XML_Parse) are a disgusting hack to
       * correct defective GPX files that Geocaching.com
       * issues as pocket queries.   They contain escape
       * characters as entities (&#x00-&#x1f) which makes
       * them not validate which croaks expat and torments
       * users.
       *
       * Look for '&' in the last maxentlength chars.   If
       * we find it, strip it, then read byte-at-a-time
       * until we find a non-entity.
       */
      char* badchar;
      char* semi;
      int maxentlength = 8;
      len = gbfread(buf, 1, MY_CBUF_SZ - maxentlength, fd);
      done = gbfeof(fd) || !len;
      buf[len] = '\0';
      if (len < maxentlength) {
        maxentlength = len;
      }
      badchar = buf+len-maxentlength;
      badchar = strchr(badchar, '&');
      extra = maxentlength - 1; /* for terminator */
      while (badchar && len < MY_CBUF_SZ-1) {
        semi = strchr(badchar, ';');
        while (extra && !semi) {
          len += gbfread(buf+len, 1, 1, fd);
          buf[len]='\0';
          extra--;
          if (buf[len-1] == ';') {
            semi= buf+len-1;
          }
        }
        badchar = strchr(badchar+1, '&');
      }
      {
        char hex[]="0123456789abcdef";
        badchar = strstr(buf, "&#x");
        while (badchar) {
          int val = 0;
          char* hexit = badchar+3;
          semi = strchr(badchar, ';');
          if (semi) {
            while (*hexit && *hexit != ';') {
              char hc = isalpha(*hexit) ? tolower(*hexit) : *hexit;
              val *= 16;
              val += strchr(hex, hc)-hex;
              hexit++;
            }

            if (val < 32) {
              warning(MYNAME ": Ignoring illegal character %s;\n\tConsider emailing %s at <%s>\n\tabout illegal characters in their GPX files.\n", badchar, gpx_author?gpx_author:"(unknown author)", gpx_email?gpx_email:"(unknown email address)");
              memmove(badchar, semi+1, strlen(semi+1)+1);
              len -= (semi-badchar)+1;
              badchar--;
            }
          }
          badchar = strstr(badchar+1, "&#x");
        }
      }
      result = XML_Parse(psr, buf, len, done);
    } else if (input_string) {
      done = 0;
      result = XML_Parse(psr, input_string,
                         input_string_len, done);
      done = 1;
    } else {
      done = 1;
      result = -1;
    }
    if (!result) {
      fatal(MYNAME ": XML parse error at line %d of '%s' : %s\n",
            (int) XML_GetCurrentLineNumber(psr),
            input_fname ? input_fname : "unknown file",
            XML_ErrorString(XML_GetErrorCode(psr)));
    }
  }
  xfree(buf);
#endif /* HAVE_LIBEXPAT */
}

static void
fprint_tag_and_attrs(const char* prefix, const char* suffix, xml_tag* tag)
{
  char** pa;
#if OLDGPX
  gbfprintf(ofd, "%s%s", prefix, tag->tagname);
#endif
  pa = tag->attributes;
  if (pa) {
    while (*pa) {
#if OLDGPX
      gbfprintf(ofd, " %s=\"%s\"", pa[0], pa[1]);
      pa += 2;
#else
     writer.writeAttribute(pa[0], pa[1]);
#endif
    }
  }
#if OLDGPX
  gbfprintf(ofd, "%s", suffix);
#else
//  writer.writeEndElement();
#endif
}

static void
fprint_xml_chain(xml_tag* tag, const waypoint* wpt)
{
  while (tag) {
    writer.writeStartElement(tag->tagname);
    if (!tag->cdata && !tag->child) {
      fprint_tag_and_attrs("<", " />", tag);
    } else {
      fprint_tag_and_attrs("<", ">", tag);

      if (tag->cdata) {
#if OLDGPX
        char* tmp_ent;
        tmp_ent = xml_entitize(tag->cdata);
        gbfprintf(ofd, "%s", tmp_ent);
        xfree(tmp_ent);
#else
      writer.writeCharacters(tag->cdata);
#endif
      }
      if (tag->child) {
        fprint_xml_chain(tag->child, wpt);
      }
      if (wpt && wpt->gc_data->exported &&
          strcmp(tag->tagname, "groundspeak:cache") == 0) {
        xml_write_time(ofd, wpt->gc_data->exported, 0,
                       "groundspeak:exported");
      }
#if OLDGPX
      gbfprintf(ofd, "</%s>\n", tag->tagname);
#else
  writer.writeEndElement();
#endif
    }
    if (tag->parentcdata) {
      // retain whitespacing, but nuke leading NL as the above will add a trailing.

      char* otmp_ent, *tmp_ent = NULL;
      otmp_ent = xml_entitize(tag->parentcdata);
      if (otmp_ent[0] == '\n')
        tmp_ent = otmp_ent+1;
      else
        tmp_ent = otmp_ent;
      gbfprintf(ofd, "%s", tmp_ent);
      xfree(otmp_ent);
    }
    tag = tag->sibling;
  }
}

void free_gpx_extras(xml_tag* tag)
{
  xml_tag* next = NULL;
  char** ap;

  while (tag) {
    if (tag->cdata) {
      xfree(tag->cdata);
    }
    if (tag->child) {
      free_gpx_extras(tag->child);
    }
    if (tag->parentcdata) {
      xfree(tag->parentcdata);
    }
    if (tag->tagname) {
      xfree(tag->tagname);
    }
    if (tag->attributes) {
      ap = tag->attributes;

      while (*ap) {
        xfree(*ap++);
      }

      xfree(tag->attributes);
    }

    next = tag->sibling;
    xfree(tag);
    tag = next;
  }
}

/*
 * Handle the grossness of GPX 1.0 vs. 1.1 handling of linky links.
 */
static void
write_gpx_url(const waypoint* waypointp)
{
  if (waypointp->url == NULL) {
    return;
  }

#if OLDGPX
  char* tmp_ent;
  if (gpx_wversion_num > 10) {
    url_link* tail;
    for (tail = (url_link*)&waypointp->url_next; tail; tail = tail->url_next) {
      tmp_ent = xml_entitize(tail->url);
      gbfprintf(ofd, "  <link href=\"%s%s\">\n",
                urlbase ? urlbase : "", tmp_ent);
      write_optional_xml_entity(ofd, "  ", "text",
                                tail->url_link_text);
      gbfprintf(ofd, "  </link>\n");
      xfree(tmp_ent);
    }
  } else {
    tmp_ent = xml_entitize(waypointp->url);
    gbfprintf(ofd, "  <url>%s%s</url>\n",
              urlbase ? urlbase : "", tmp_ent);
    write_optional_xml_entity(ofd, "  ", "urlname",
                              waypointp->url_link_text);
    xfree(tmp_ent);
  }
#else
  if (gpx_wversion_num > 10) {
    url_link* tail;
    for (tail = (url_link*)&waypointp->url_next; tail; tail = tail->url_next) {
      writer.writeStartElement("link");
        writer.writeAttribute("href", tail->url);
        writer.writeOptionalTextElement("text", tail->url_link_text);
      // FIXME This is to force empty links to not be self-closing.  This is
      // lame, but it's for compatibilty with our old writer to minimize thrash
      // on the Qt transition.
      writer.writeCharacters("\n");
      writer.writeEndElement();
    }
    return;
  }
  writer.writeTextElement("url", QString(urlbase) + QString(waypointp->url));
  writer.writeOptionalTextElement("urlname", QString(waypointp->url_link_text));
#endif
}

/*
 * Write optional accuracy information for a given (way|track|route)point
 * to the output stream.  Done in one place since it's common for all three.
 * Order counts.
 */
static void
gpx_write_common_acc(const waypoint* waypointp, const char* indent)
{
  const char* fix = NULL;

  switch (waypointp->fix) {
  case fix_2d:
    fix = "2d";
    break;
  case fix_3d:
    fix = "3d";
    break;
  case fix_dgps:
    fix = "dgps";
    break;
  case fix_pps:
    fix = "pps";
    break;
  case fix_none:
    fix = "none";
    break;
    /* GPX spec says omit if we don't know. */
  case fix_unknown:
  default:
    break;
  }
#if OLDGPX
  if (fix) {
    gbfprintf(ofd, "%s<fix>%s</fix>\n", indent, fix);
  }
  if (waypointp->sat > 0) {
    gbfprintf(ofd, "%s<sat>%d</sat>\n", indent, waypointp->sat);
  }
  if (waypointp->hdop) {
    gbfprintf(ofd, "%s<hdop>%f</hdop>\n", indent, waypointp->hdop);
  }
  if (waypointp->vdop) {
    gbfprintf(ofd, "%s<vdop>%f</vdop>\n", indent, waypointp->vdop);
  }
  if (waypointp->pdop) {
    gbfprintf(ofd, "%s<pdop>%f</pdop>\n", indent, waypointp->pdop);
  }
#else
  if (fix) writer.writeTextElement("fix", fix);
  if (waypointp->sat > 0) {
    writer.writeTextElement("sat", QString::number(waypointp->sat));
  }
  if (waypointp->hdop) {
    writer.writeTextElement("hdop", QString::number(waypointp->hdop, 'f', 6));
  }
  if (waypointp->vdop) {
    writer.writeTextElement("vdop", QString::number(waypointp->vdop, 'f', 6));
  }
  if (waypointp->pdop) {
    writer.writeTextElement("pdop", QString::number(waypointp->pdop, 'f', 6));
  }
#endif
}


static void
gpx_write_common_position(const waypoint* waypointp, const char* indent)
{
  if (waypointp->altitude != unknown_alt) {
#if OLDGPX
    gbfprintf(ofd, "%s<ele>%f</ele>\n",
              indent, waypointp->altitude);
#else
     writer.writeTextElement("ele", QString::number(waypointp->altitude, 'f', 6));
#endif
  }
  if (waypointp->creation_time) {
#if OLDGPX
    gbfprintf(ofd, indent);
    xml_write_time(ofd, waypointp->creation_time, waypointp->microseconds, "time");
#else
     char time_string[64];
     // FIXME: Eventually use creation_time.toString()
     xml_fill_in_time(time_string, waypointp->creation_time, waypointp->microseconds, XML_LONG_TIME);
     if (time_string[0]) {
       writer.writeTextElement("time", time_string);
     }
#endif
  }
}

static void
gpx_write_common_extensions(const waypoint* waypointp, const char* indent)
{
  if (((opt_humminbirdext || opt_garminext) && (waypointp->depth != 0 || waypointp->temperature != 0))
      || (opt_garminext && (waypointp->heartrate != 0 || waypointp->cadence != 0))) {
#if OLDGPX
    gbfprintf(ofd, "%s<extensions>\n", indent);
    if (waypointp->depth != 0) {
      if (opt_humminbirdext)
        gbfprintf(ofd, "%s  <h:depth>%f</h:depth>\n",
                  indent, waypointp->depth*100.0);
      if (opt_garminext)
        gbfprintf(ofd, "%s  <gpxx:Depth>%f</gpxx:Depth>\n",
                  indent, waypointp->depth);
    }
    if (waypointp->temperature != 0) {
      if (opt_humminbirdext)
        gbfprintf(ofd, "%s  <h:temperature>%f</h:temperature>\n",
                  indent, waypointp->temperature);
      if (opt_garminext)
        gbfprintf(ofd, "%s  <gpxx:Temperature>%f</gpxx:Temperature>\n",
                  indent, waypointp->temperature);
    }
    if (opt_garminext && (waypointp->heartrate != 0 || waypointp->cadence != 0)) {
      gbfprintf(ofd, "%s  <gpxtpx:TrackPointExtension>\n", indent);
      if (waypointp->heartrate != 0)
        gbfprintf(ofd, "%s    <gpxtpx:hr>%u</gpxtpx:hr>\n",
                  indent, waypointp->heartrate);
      if (waypointp->cadence != 0)
        gbfprintf(ofd, "%s    <gpxtpx:cad>%u</gpxtpx:cad>\n",
                  indent, waypointp->cadence);
      gbfprintf(ofd, "%s  </gpxtpx:TrackPointExtension>\n", indent);
    }
    gbfprintf(ofd, "%s</extensions>\n", indent);
#else
    writer.writeStartElement("extensions");
    if (waypointp->depth != 0) {
      if (opt_humminbirdext) {
        writer.writeTextElement("h:depth", QString::number(waypointp->depth * 100.0));
      }
      if (opt_garminext) {
        writer.writeTextElement("gpxx:Depth", QString::number(waypointp->depth));
      }
    }
    if (waypointp->temperature != 0) {
      if (opt_humminbirdext) {
        writer.writeTextElement("h:temperature", QString::number(waypointp->temperature));
      }
      if (opt_garminext) {
        writer.writeTextElement("gpxx:Temperature", QString::number(waypointp->temperature));
      }
    }
    if (opt_garminext && (waypointp->heartrate != 0 || waypointp->cadence != 0)) {
      writer.writeStartElement("gpxtpx:TrackPointExtension");
      if (waypointp->heartrate != 0) {
        writer.writeTextElement("gpxtpx:hr", QString::number(waypointp->heartrate));
      }
      if (waypointp->cadence != 0) {
        writer.writeTextElement("gpxtpx:cad", QString::number(waypointp->cadence));
      }
      writer.writeEndElement(); // "gpxtpx:TrackPointExtension"
    }
    writer.writeEndElement(); // "extensions"
#endif
  }
}

static void
gpx_write_common_description(const waypoint* waypointp, const char* indent,
                             const char* oname)
{
#if OLDGPX
  write_optional_xml_entity(ofd, indent, "name", oname);
  write_optional_xml_entity(ofd, indent, "cmt", waypointp->description);
  if (waypointp->notes && waypointp->notes[0]) {
    write_xml_entity(ofd, indent, "desc", waypointp->notes);
  } else {
    write_optional_xml_entity(ofd, indent, "desc", waypointp->description);
  }
  write_gpx_url(waypointp);
  write_optional_xml_entity(ofd, indent , "sym", waypointp->icon_descr);
#else
  if (oname) {
    writer.writeTextElement("name", oname);
  }
  writer.writeOptionalTextElement("cmt", waypointp->description);
  if (waypointp->notes && waypointp->notes[0]) {
    writer.writeTextElement("desc", waypointp->notes);
  } else {
    writer.writeOptionalTextElement("desc", waypointp->description);
  }
  write_gpx_url(waypointp);
  if (!waypointp->icon_descr.isNull()) {
    writer.writeTextElement("sym", waypointp->icon_descr);
  }
#endif
}

static void
gpx_waypt_pr(const waypoint* waypointp)
{
  const char* oname;
  fs_xml* fs_gpx;
  garmin_fs_t* gmsd;	/* gARmIN sPECIAL dATA */
#if OLDGPX
  gbfprintf(ofd, "<wpt lat=\"" FLT_FMT "\" lon=\"" FLT_FMT "\">\n",
            waypointp->latitude,
            waypointp->longitude);
#else
  writer.writeStartElement("wpt");
  writer.writeAttribute("lat", QString::number(waypointp->latitude, 'f', 9));
  writer.writeAttribute("lon", QString::number(waypointp->longitude, 'f', 9));
#endif
  oname = global_opts.synthesize_shortnames ?
          mkshort_from_wpt(mkshort_handle, waypointp) :
          waypointp->shortname;
  gpx_write_common_position(waypointp, "  ");
  gpx_write_common_description(waypointp, "  ", oname);
  gpx_write_common_acc(waypointp, "  ");

  fs_gpx = (fs_xml*)fs_chain_find(waypointp->fs, FS_GPX);
  gmsd = GMSD_FIND(waypointp);
  if (fs_gpx) {
    if (! gmsd) {
      fprint_xml_chain(fs_gpx->tag, waypointp);
    }
  }
  if (gmsd && (gpx_wversion_num > 10)) {
    /* MapSource doesn't accepts extensions from 1.0 */
    garmin_fs_xml_fprint(ofd, waypointp, writer);
  }
  gpx_write_common_extensions(waypointp, "  ");
#if OLDGPX
  gbfprintf(ofd, "</wpt>\n");
#else
  writer.writeEndElement();
#endif
}

static void
gpx_track_hdr(const route_head* rte)
{
  fs_xml* fs_gpx;
  current_trk_head = rte;
#if OLDGPX
  gbfprintf(ofd, "<trk>\n");
  write_optional_xml_entity(ofd, "  ", "name", rte->rte_name);
  write_optional_xml_entity(ofd, "  ", "desc", rte->rte_desc);
  if (rte->rte_num) {
    gbfprintf(ofd, "<number>%d</number>\n", rte->rte_num);
  }

  if (gpx_wversion_num > 10) {
    fs_gpx = (fs_xml*)fs_chain_find(rte->fs, FS_GPX);
    if (fs_gpx) {
      fprint_xml_chain(fs_gpx->tag, NULL);
    }
  }
#else
  writer.writeStartElement("trk");
  writer.writeOptionalTextElement("name", rte->rte_name);
  writer.writeOptionalTextElement("desc", rte->rte_desc);
  if (rte->rte_num) {
    writer.writeTextElement("number", QString::number(rte->rte_num));
  }
  // TODO the chain nonsense.
#endif
}

static void
gpx_track_disp(const waypoint* waypointp)
{
  const char* oname;
  fs_xml* fs_gpx;
  int first_in_trk;
  first_in_trk = waypointp->Q.prev == &current_trk_head->waypoint_list;

  if (waypointp->wpt_flags.new_trkseg) {
    if (!first_in_trk) {
#if OLDGPX
      gbfprintf(ofd, "</trkseg>\n");
#else
      writer.writeEndElement();
#endif
    }
#if OLDGPX
    gbfprintf(ofd, "<trkseg>\n");
#else
    writer.writeStartElement("trkseg");
#endif
  }
#if OLDGPX
  gbfprintf(ofd, "<trkpt lat=\"" FLT_FMT_T "\" lon=\"" FLT_FMT_T "\">\n",
            waypointp->latitude,
            waypointp->longitude);
#else
  writer.writeStartElement("trkpt");
  writer.writeAttribute("lat", QString::number(waypointp->latitude, 'f', 9));
  writer.writeAttribute("lon", QString::number(waypointp->longitude, 'f', 9));
  // FIXME: this is technically not needed.  
  // Our Pre-Qt XML writer would write
  // <trkpt lat=X lon=Y>
  // </trkpt>
  // even thogh <trkpt lat=X lon=Y /> is more compact and legal (assuming there
  // are no child tags, of course.  This is to bridge our exsiting test suite
  // during the transition to QXmlStreamWriter to force an explict closing tag.
  writer.writeCharacters("\n");
#endif

  gpx_write_common_position(waypointp, "  ");

  /* These were accidentally removed from 1.1 */
  if (gpx_wversion_num == 10) {
    if WAYPT_HAS(waypointp, course) {
#if OLDGPX
      gbfprintf(ofd, "  <course>%f</course>\n",
                waypointp->course);
#else
      writer.writeTextElement("course", QString::number(waypointp->course, 'f', 6));
#endif
    }
    if WAYPT_HAS(waypointp, speed) {
#if OLDGPX
      gbfprintf(ofd, "  <speed>%f</speed>\n",
                waypointp->speed);
#else
      writer.writeTextElement("speed", QString::number(waypointp->speed, 'f', 6));
#endif
    }
  }

  /* GPX doesn't require a name on output, so if we made one up
   * on input, we might as well say nothing.
   */
  oname = global_opts.synthesize_shortnames ?
          mkshort_from_wpt(mkshort_handle, waypointp) :
          waypointp->shortname;
  gpx_write_common_description(waypointp, "  ",
                               waypointp->wpt_flags.shortname_is_synthetic ?
                               NULL : oname);
  gpx_write_common_acc(waypointp, "  ");

  fs_gpx = (fs_xml*)fs_chain_find(waypointp->fs, FS_GPX);
  if (fs_gpx) {
    fprint_xml_chain(fs_gpx->tag, waypointp);
  }

  gpx_write_common_extensions(waypointp, "  ");
#if OLDGPX
  gbfprintf(ofd, "</trkpt>\n");
#else
  writer.writeEndElement();
#endif
}

static void
gpx_track_tlr(const route_head* rte)
{
  if (!QUEUE_EMPTY(&current_trk_head->waypoint_list)) {
#if OLDGPX
    gbfprintf(ofd, "</trkseg>\n");
#else
    writer.writeEndElement();
#endif
  }
#if OLDGPX
  gbfprintf(ofd, "</trk>\n");
#else
    // FIXME This is to force empty tracks to not be self-closing.  This is
    // lame, but it's for compatibilty with our old writer to minimize thrash
    // on the Qt transition.
    writer.writeCharacters("\n");
    writer.writeEndElement();
#endif
  current_trk_head = NULL;
}

static
void gpx_track_pr()
{
  track_disp_all(gpx_track_hdr, gpx_track_tlr, gpx_track_disp);
}

static void
gpx_route_hdr(const route_head* rte)
{
  fs_xml* fs_gpx;
#if OLDGPX
  gbfprintf(ofd, "<rte>\n");
#else
  writer.writeStartElement("rte");
#endif
#if OLDGPX
  write_optional_xml_entity(ofd, "  ", "name", rte->rte_name);
  write_optional_xml_entity(ofd, "  ", "desc", rte->rte_desc);
#else
  writer.writeOptionalTextElement("name", rte->rte_name);
  writer.writeOptionalTextElement("desc", rte->rte_desc);
#endif
  if (rte->rte_num) {
#if OLDGPX
    gbfprintf(ofd, "  <number>%d</number>\n", rte->rte_num);
#else
    writer.writeTextElement("number", QString::number(rte->rte_num));
#endif
  }

  if (gpx_wversion_num > 10) {
    fs_gpx = (fs_xml*)fs_chain_find(rte->fs, FS_GPX);
    if (fs_gpx) {
      fprint_xml_chain(fs_gpx->tag, NULL);
    }
  }
}

static void
gpx_route_disp(const waypoint* waypointp)
{
  const char* oname;
  fs_xml* fs_gpx;
#if OLDGPX
  gbfprintf(ofd, "  <rtept lat=\"" FLT_FMT_R "\" lon=\"" FLT_FMT_R "\">\n",
            waypointp->latitude,
            waypointp->longitude);
#else
  writer.writeStartElement("rtept");
  writer.writeAttribute("lat", QString::number(waypointp->latitude, 'f', 9));
  writer.writeAttribute("lon", QString::number(waypointp->longitude, 'f', 9));
#endif

  oname = global_opts.synthesize_shortnames ?
          mkshort_from_wpt(mkshort_handle, waypointp) :
          waypointp->shortname;
  gpx_write_common_position(waypointp, "    ");
  gpx_write_common_description(waypointp, "    ", oname);
  gpx_write_common_acc(waypointp, "    ");

  fs_gpx = (fs_xml*)fs_chain_find(waypointp->fs, FS_GPX);
  if (fs_gpx) {
    fprint_xml_chain(fs_gpx->tag, waypointp);
  }

  gpx_write_common_extensions(waypointp, "    ");
#if OLDGPX
  gbfprintf(ofd, "  </rtept>\n");
#else
  writer.writeEndElement();
#endif
}

static void
gpx_route_tlr(const route_head* rte)
{
#if OLDGPX
  gbfprintf(ofd, "</rte>\n");
#else
  // FIXME This is to force empty tracks to not be self-closing.  This is
  // lame, but it's for compatibilty with our old writer to minimize thrash
  // on the Qt transition.
  writer.writeCharacters("\n");
  writer.writeEndElement(); // Close rte tag.
#endif
}

static
void gpx_route_pr()
{
  /* output routes */
  route_disp_all(gpx_route_hdr, gpx_route_tlr, gpx_route_disp);
}

static void
gpx_waypt_bound_calc(const waypoint* waypointp)
{
  waypt_add_to_bounds(&all_bounds, waypointp);
}

static void
gpx_write_bounds(void)
{
  waypt_init_bounds(&all_bounds);

  waypt_disp_all(gpx_waypt_bound_calc);
  route_disp_all(NULL, NULL, gpx_waypt_bound_calc);
  track_disp_all(NULL, NULL, gpx_waypt_bound_calc);

  if (waypt_bounds_valid(&all_bounds)) {
#if OLDGPX
    gbfprintf(ofd, "<bounds minlat=\"%0.9f\" minlon=\"%0.9f\" "
              "maxlat=\"%0.9f\" maxlon=\"%0.9f\"/>\n",
              all_bounds.min_lat, all_bounds.min_lon,
              all_bounds.max_lat, all_bounds.max_lon);
#else
  writer.writeStartElement("bounds");
  writer.writeAttribute("minlat", QString::number(all_bounds.min_lat, 'f', 9));
  writer.writeAttribute("minlon", QString::number(all_bounds.min_lon, 'f', 9));
  writer.writeAttribute("maxlat", QString::number(all_bounds.max_lat, 'f', 9));
  writer.writeAttribute("maxlon", QString::number(all_bounds.max_lon, 'f', 9));
  writer.writeEndElement();
#endif
  }
}

static void
gpx_write(void)
{
  time_t now = 0;

  /* if an output version is not specified and an input version is
   * available use it, otherwise use the default.
   */

  if (! gpx_wversion) {
    if (! gpx_version) {
      gpx_wversion = (char *)"1.0";
    } else {
      gpx_wversion = (char *)gpx_version;
    }
  }

  if (opt_humminbirdext || opt_garminext) {
    gpx_wversion = (char*)"1.1";
  }

  gpx_wversion_num = strtod(gpx_wversion, NULL) * 10;

  if (gpx_wversion_num <= 0) {
    fatal(MYNAME ": gpx version number of '%s' not valid.\n", gpx_wversion);
  }

  now = current_time();
#if OLDGPX
  gbfprintf(ofd, "<?xml version=\"1.0\" encoding=\"%s\"?>\n", global_opts.charset_name);
  gbfprintf(ofd, "<gpx\n  version=\"%s\"\n", gpx_wversion);
  gbfprintf(ofd, "  creator=\"" CREATOR_NAME_URL "\"\n");
  gbfprintf(ofd, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
#else
  // FIXME: This write of a blank line is needed for Qt 4.6 (as on Centos 6.3)
  // to include just enough whitespace between <xml/> and <gpx...> to pass
  // diff -w.  It's here for now to shim compatibility with our zillion 
  // reference files, but this blank link can go away some day.
  writer.writeCharacters("\n");

  writer.setAutoFormatting(true);
  writer.writeStartElement("gpx");
  writer.writeAttribute("\n  version", gpx_wversion);
  writer.writeAttribute("\n  creator", CREATOR_NAME_URL);
  writer.writeAttribute("\n  xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
#endif

  if (opt_humminbirdext) {
#if OLDGPX
    gbfprintf(ofd, "  xmlns:h=\"http://humminbird.com\"\n");
#else
    writer.writeAttribute("\n  xmlns:h","http://humminbird.com");
#endif
  }
  if (opt_garminext) {
#if OLDGPX
    gbfprintf(ofd, "  xmlns:gpxx=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\"\n"
              "  xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\"\n");
#else
    writer.writeAttribute("\n  xmlns:gpxx", "http://www.garmin.com/xmlschemas/GpxExtensions/v3");
    writer.writeAttribute("\n  xmlns:gpxtpx", "http://www.garmin.com/xmlschemas/TrackPointExtension/v1");
#endif
  }
#if OLDGPX
  gbfprintf(ofd, "  xmlns=\"http://www.topografix.com/GPX/%c/%c\"\n", gpx_wversion[0], gpx_wversion[2]);
#else
  writer.writeAttribute("\n  xmlns", QString("http://www.topografix.com/GPX/%1/%2").arg(gpx_wversion[0]).arg(gpx_wversion[2]));
#endif
  if (xsi_schema_loc) {
#if OLDGPX
    gbfprintf(ofd, "  xsi:schemaLocation=\"%s\">\n", xsi_schema_loc);
#else
    writer.writeAttribute("\n  xsi:schemaLocation", xsi_schema_loc);
#endif
  } else {
#if OLDGPX
    gbfprintf(ofd,
              "  xsi:schemaLocation=" DEFAULT_XSI_SCHEMA_LOC_FMT">\n",
              gpx_wversion[0], gpx_wversion[2],
              gpx_wversion[0], gpx_wversion[2]);
#else
    writer.writeAttribute("\n  xsi:schemaLocation", 
       QString("http://www.topografix.com/GPX/%1/%2 http://www.topografix.com/GPX/%1/%2/gpx.xsd").arg(gpx_wversion[0]).arg(gpx_wversion[2]));
#endif
  }

  if (gpx_wversion_num > 10) {
#if OLDGPX
    gbfprintf(ofd, "<metadata>\n");
#else
    writer.writeStartElement("metadata");
#endif
  }
  gpx_write_gdata(&gpx_global->name, "name");
  gpx_write_gdata(&gpx_global->desc, "desc");
  /* In GPX 1.1, author changed from a string to a PersonType.
   * since it's optional, we just drop it instead of rewriting it.
   */
  if (gpx_wversion_num < 11) {
    gpx_write_gdata(&gpx_global->author, "author");
  }
  /* In GPX 1.1 email, url, urlname aren't allowed. */
  if (gpx_wversion_num < 11) {
    gpx_write_gdata(&gpx_global->email, "email");
    gpx_write_gdata(&gpx_global->url, "url");
    gpx_write_gdata(&gpx_global->urlname, "urlname");
  }
#if OLDGPX
  xml_write_time(ofd, now, 0, "time");
#else
   char time_string[64];
   xml_fill_in_time(time_string, now, 0, XML_LONG_TIME);
   if (time_string[0]) {
     writer.writeTextElement("time", time_string);
   }
#endif
  gpx_write_gdata(&gpx_global->keywords, "keywords");

  gpx_write_bounds();

  if (gpx_wversion_num > 10) {
#if OLDGPX
    gbfprintf(ofd, "</metadata>\n");
#else
    writer.writeEndElement();
#endif
  }

  gpx_reset_short_handle();
  waypt_disp_all(gpx_waypt_pr);
  gpx_reset_short_handle();
  gpx_route_pr();
  gpx_reset_short_handle();
  gpx_track_pr();
#if OLDGPX
  gbfprintf(ofd, "</gpx>\n");
#else
  writer.writeEndElement(); // Close gpx tag.
#endif
}


static void
gpx_free_gpx_global(void)
{
  gpx_rm_from_global(&gpx_global->name);
  gpx_rm_from_global(&gpx_global->desc);
  gpx_rm_from_global(&gpx_global->author);
  gpx_rm_from_global(&gpx_global->email);
  gpx_rm_from_global(&gpx_global->url);
  gpx_rm_from_global(&gpx_global->urlname);
  gpx_rm_from_global(&gpx_global->keywords);
  xfree(gpx_global);
}

static void
gpx_exit(void)
{
  if (gpx_version) {
    xfree(gpx_version);
    gpx_version = NULL;
  }

  if (xsi_schema_loc) {
    xfree(xsi_schema_loc);
    xsi_schema_loc = NULL;
  }

  if (gpx_global) {
    gpx_free_gpx_global();
    gpx_global = NULL;
  }
}

static
arglist_t gpx_args[] = {
  {
    "snlen", &snlen, "Length of generated shortnames",
    "32", ARGTYPE_INT, "1", NULL
  },
  {
    "suppresswhite", &suppresswhite,
    "No whitespace in generated shortnames",
    NULL, ARGTYPE_BOOL, ARG_NOMINMAX
  },
  {
    "logpoint", &opt_logpoint,
    "Create waypoints from geocache log entries",
    NULL, ARGTYPE_BOOL, ARG_NOMINMAX
  },
  {
    "urlbase", &urlbase, "Base URL for link tag in output",
    NULL, ARGTYPE_STRING, ARG_NOMINMAX
  },
  {
    "gpxver", &gpx_wversion, "Target GPX version for output",
    NULL, ARGTYPE_STRING, ARG_NOMINMAX
  },
  {
    "humminbirdextensions", &opt_humminbirdext,
    "Add info (depth) as Humminbird extension",
    NULL, ARGTYPE_BOOL, ARG_NOMINMAX
  },
  {
    "garminextensions", &opt_garminext,
    "Add info (depth) as Garmin extension",
    NULL, ARGTYPE_BOOL, ARG_NOMINMAX
  },
  ARG_TERMINATOR
};

ff_vecs_t gpx_vecs = {
  ff_type_file,
  FF_CAP_RW_ALL,
  gpx_rd_init,
  gpx_wr_init,
  gpx_rd_deinit,
  gpx_wr_deinit,
  gpx_read,
  gpx_write,
  gpx_exit,
  gpx_args,
  CET_CHARSET_UTF8, 0	/* non-fixed to create non UTF-8 XML's for testing | CET-REVIEW */
};
