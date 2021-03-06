/*
** Copyright (C) 2009-2015 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2015 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* sagan-perfmon.c
*
* This write out statistics to a CSV type file so often (user defined).  If
* enabled,  this thread never exits
*
*/

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>

#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-config.h"

struct _SaganConfig *config;
struct _SaganCounters *counters;

void Sagan_Perfmonitor_Handler( void )
{

    unsigned long total=0;
    unsigned long seconds=0;

    char curtime[64] = { 0 };
    char curtime_utime[64] = { 0 };
    time_t t;
    struct tm *now;

    t = time(NULL);
    now=localtime(&t);
    strftime(curtime, sizeof(curtime), "%m/%d/%Y %H:%M:%S",  now);
    strftime(curtime_utime, sizeof(curtime_utime), "%s",  now);

    uint64_t last_sagantotal = 0;
    uint64_t last_saganfound = 0;
    uint64_t last_alert_total = 0;
    uint64_t last_after_total = 0;
    uint64_t last_threshold_total = 0;
    uint64_t last_sagan_processor_drop = 0;
    uint64_t last_ignore_count = 0;

#ifdef HAVE_LIBGEOIP
    uint64_t last_geoip_lookup = 0;
    uint64_t last_geoip_hit = 0;
    uint64_t last_geoip_miss = 0;
#endif

#ifdef WITH_WEBSENSE
    uint64_t last_websense_cache_hit = 0;
    uint64_t last_websense_ignore_hit = 0;
    uint64_t last_websense_error_count = 0;
    uint64_t last_websense_positive_hit = 0;
#endif

#ifdef HAVE_LIBESMTP
    uint64_t last_esmtp_count_success = 0;
    uint64_t last_esmtp_count_failed = 0;
#endif

    uint64_t last_blacklist_hit_count = 0;
    uint64_t last_search_case_hit_count = 0;
    uint64_t last_search_nocase_hit_count = 0;
    uint64_t last_sagan_output_drop = 0;

    uint64_t last_dns_miss_count = 0;

    fprintf(config->perfmonitor_file_stream, "################################ Perfmon start: pid=%d at=%s ###################################\n", getpid(), curtime);
    fprintf(config->perfmonitor_file_stream, "# engine.utime,engine.total,engine.sig_match.total,engine.alerts.total,engine.after.total,engine.threshold.total, engine.drop.total,engine.ignored.total,engine.eps,geoip.lookup.total,geoip.hits,geoip.misses,processor.drop.total,processor.blacklist.hits,processor.search.case.hits,processor.search.nocase.hits,processor.tracker.total,processor.tracker.down,output.drop.total,processor.esmtp.success,processor.esmtp.failed,dns.total,dns.miss,processor.websense.cache_count,processor.websense.hits,processor.websense.ignored,processor.websense.errors,processor.websense.found\n");
    fflush(config->perfmonitor_file_stream);

    while (1)
        {

            sleep(config->perfmonitor_time);

            fprintf(config->perfmonitor_file_stream, "%s,", curtime_utime),

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->sagantotal - last_sagantotal);
            last_sagantotal = counters->sagantotal;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->saganfound - last_saganfound);
            last_saganfound = counters->saganfound;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->alert_total - last_alert_total);
            last_alert_total = counters->alert_total;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->after_total - last_after_total);
            last_after_total = counters->after_total;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->threshold_total - last_threshold_total);
            last_threshold_total = counters->threshold_total;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->sagan_processor_drop - last_sagan_processor_drop);
            last_sagan_processor_drop = counters->sagan_processor_drop;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->ignore_count - last_ignore_count);
            last_ignore_count = counters->ignore_count;

            t = time(NULL);
            now=localtime(&t);
            strftime(curtime_utime, sizeof(curtime_utime), "%s",  now);
            seconds = atol(curtime_utime) - atol(config->sagan_startutime);
            total = counters->sagantotal / seconds;

            fprintf(config->perfmonitor_file_stream, "%lu,", total);

#ifdef HAVE_LIBGEOIP

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->geoip_lookup - last_geoip_lookup);
            last_geoip_lookup = counters->geoip_lookup;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->geoip_hit - last_geoip_hit);
            last_geoip_hit = counters->geoip_hit;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->geoip_miss - last_geoip_miss);
            last_geoip_miss = counters->geoip_miss;

#endif

#ifndef HAVE_LIBGEOIP

            fprintf(config->perfmonitor_file_stream, "0,0,0,");

#endif

            /* DEBUG: IS THE BELOW RIGHT?  TWO counters->sagan_processor_drop REFERENCES */

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->sagan_processor_drop - last_sagan_processor_drop);
            last_sagan_processor_drop = counters->sagan_processor_drop;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->blacklist_hit_count - last_blacklist_hit_count);
            last_blacklist_hit_count = counters->blacklist_hit_count;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->search_case_hit_count - last_search_case_hit_count);
            last_search_case_hit_count = counters->search_case_hit_count;

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->search_nocase_hit_count - last_search_nocase_hit_count);
            last_search_nocase_hit_count = counters->search_nocase_hit_count;

            /* DEBUG: CONSTANT? */

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->track_clients_client_count);
            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->track_clients_down);

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->sagan_output_drop - last_sagan_output_drop);
            last_sagan_output_drop = counters->sagan_output_drop;

#ifdef HAVE_LIBESMTP
            if ( config->sagan_esmtp_flag )
                {

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->esmtp_count_success - last_esmtp_count_success);
                    last_esmtp_count_success = counters->esmtp_count_success;

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->esmtp_count_failed - last_esmtp_count_failed);
                    last_esmtp_count_failed = counters->esmtp_count_failed;
                }
            else
                {
                    fprintf(config->perfmonitor_file_stream, "0,0,");
                }
#endif

#ifndef HAVE_LIBESMTP
            fprintf(config->perfmonitor_file_stream, "0,0,");
#endif

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->dns_cache_count);

            fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->dns_miss_count - last_dns_miss_count);
            last_dns_miss_count = counters->dns_miss_count;

#ifdef WITH_WEBSENSE

            if (config->websense_flag)
                {

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->websense_cache_count);

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->websense_cache_hit - last_websense_cache_hit);
                    last_websense_cache_hit = counters->websense_cache_hit;

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->websense_ignore_hit - last_websense_ignore_hit);
                    last_websense_ignore_hit = counters->websense_ignore_hit;

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 ",", counters->websense_error_count - last_websense_error_count);
                    last_websense_error_count = counters->websense_error_count;

                    fprintf(config->perfmonitor_file_stream, "%" PRIu64 "", counters->websense_postive_hit - last_websense_positive_hit); 	/* Don't need , here */
                    last_websense_positive_hit = counters->websense_postive_hit;

                }
            else
                {

                    fprintf(config->perfmonitor_file_stream, "0,0,0,0,0");
                }

#endif

#ifndef WITH_WEBSENSE
            fprintf(config->perfmonitor_file_stream, "0,0,0,0,0");
#endif

            fprintf(config->perfmonitor_file_stream, "\n");
            fflush(config->perfmonitor_file_stream);
        }
}

void Sagan_Perfmonitor_Exit(void)
{

    char curtime[64] = { 0 };

    time_t t;
    struct tm *now;

    t = time(NULL);
    now=localtime(&t);
    strftime(curtime, sizeof(curtime), "%m/%d/%Y %H:%M:%S",  now);

    fprintf(config->perfmonitor_file_stream, "################################ Perfmon end: pid=%d at=%s ###################################\n", getpid(), curtime);

    fflush(config->perfmonitor_file_stream);
    fclose(config->perfmonitor_file_stream);

}
