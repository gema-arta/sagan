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

/* sagan-rules.c
 *
 * Loads and parses the rule files into memory
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <pcre.h>

#include "version.h"

#include "sagan.h"
#include "sagan-defs.h"

#include "sagan-flowbit.h"
#include "sagan-lockfile.h"
#include "sagan-classifications.h"
#include "sagan-rules.h"
#include "sagan-config.h"
#include "parsers/parsers.h"

struct _SaganCounters *counters;
struct _SaganDebug *debug;
struct _SaganConfig *config;

#ifdef HAVE_LIBLOGNORM
#include "sagan-liblognorm.h"
struct liblognorm_struct *liblognormstruct;
struct liblognorm_toload_struct *liblognormtoloadstruct;
int liblognorm_count;
#endif

struct _Rule_Struct *rulestruct;
struct _Class_Struct *classstruct;
struct _Sagan_Flowbit *flowbit;

void Load_Rules( const char *ruleset )
{

    const char *error;
    int erroffset;

    FILE *rulesfile;

    char *rulestring;
    char *netstring;
    char *nettmp = NULL;

    char *tokenrule;
    char *tokennet;
    char *rulesplit;
    char *arg;
    char *saveptrnet;
    char *saveptrrule1;
    char *saveptrrule2;
    char *saveptrrule3=NULL;
    char *tmptoken;
    char *not;
    char *savenot=NULL;

    char *tok_tmp;
    char *tmptok_tmp;

    unsigned char fwsam_time_tmp;

    char netstr[RULEBUF];

    /* line added by drforbin array should be initialized */
    memset(netstr, 0, RULEBUF);
    char rulestr[RULEBUF];
    /* line added by drforbin array should be initialized */

    memset(rulestr, 0, RULEBUF);
    char rulebuf[RULEBUF];
    char pcrerule[RULEBUF];
    char tmp2[512];
    char tmp[2];
    char final_content[512] = { 0 };

    char alert_time_tmp1[10];
    char alert_time_tmp2[3];

    int linecount=0;
    int netcount=0;
    int ref_count=0;

    int content_count=0;
    int meta_content_count=0;
    int pcre_count=0;
    int flowbit_count;

    sbool pcreflag=0;
    int pcreoptions=0;

    int i=0;
    int a=0;

    int rc=0;

    int forward=0;
    int reverse=0;

    /* Rule vars */

    int ip_proto=0;
    int dst_port=0;
    int src_port=0;

#ifdef HAVE_LIBLOGNORM
    sbool liblognorm_flag=0;
#endif

    if (( rulesfile = fopen(ruleset, "r" )) == NULL )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open rule file (%s)", __FILE__, __LINE__, ruleset);
        }

    Sagan_Log(S_NORMAL, "Loading %s rule file", ruleset);

    while (fgets(rulebuf, sizeof(rulebuf), rulesfile) != NULL )
        {

            linecount++;

            if (rulebuf[0] == '#' || rulebuf[0] == 10 || rulebuf[0] == ';' || rulebuf[0] == 32)
                {
                    continue;
                }
            else
                {
                    /* Allocate memory for rules, but not comments */
                    rulestruct = (_Rule_Struct *) realloc(rulestruct, (counters->rulecount+1) * sizeof(_Rule_Struct));
                }

            Remove_Return(rulebuf);

            /****************************************/
            /* Some really basic rule sanity checks */
            /****************************************/

            if (!strchr(rulebuf, ';') || !strchr(rulebuf, ':') ||
                    !strchr(rulebuf, '(') || !strchr(rulebuf, ')')) Sagan_Log(S_ERROR, "[%s, line %d]  %s on line %d appears to be incorrect.", __FILE__, __LINE__, ruleset, linecount);

            if (!Sagan_strstr(rulebuf, "sid:")) Sagan_Log(S_ERROR, "[%s, line %d] %s on line %d appears to not have a 'sid'", __FILE__, __LINE__, ruleset, linecount);
            if (!Sagan_strstr(rulebuf, "rev:")) Sagan_Log(S_ERROR, "[%s, line %d] %s on line %d appears to not have a 'rev'", __FILE__, __LINE__, ruleset, linecount);
            if (!Sagan_strstr(rulebuf, "msg:")) Sagan_Log(S_ERROR, "[%s, line %d] %s on line %d appears to not have a 'msg'", __FILE__, __LINE__, ruleset, linecount);

            rc=0;
            if (!Sagan_strstr(rulebuf, "alert")) rc++;
            if (!Sagan_strstr(rulebuf, "drop")) rc++;
            if ( rc == 2 ) Sagan_Log(S_ERROR, "[%s, line %d] %s on line %d appears to not have a 'alert' or 'drop'", __FILE__, __LINE__, ruleset, linecount);

            rc=0;
            if (!Sagan_strstr(rulebuf, "tcp")) rc++;
            if (!Sagan_strstr(rulebuf, "udp")) rc++;
            if (!Sagan_strstr(rulebuf, "icmp")) rc++;
            if (!Sagan_strstr(rulebuf, "syslog")) rc++;
            if ( rc == 4 ) Sagan_Log(S_ERROR, "[%s, line %d] %s on line %d appears to not have a protocol type (tcp/udp/icmp/syslog)", __FILE__, __LINE__, ruleset, linecount);

            /* Parse forward for the first '(' */

            for (i=0; i<strlen(rulebuf); i++)
                {
                    if ( rulebuf[i] == '(' )
                        {
                            forward=i;
                            break;
                        }
                }

            /* Parse reverse for the first ')' */

            for (i=strlen(rulebuf); i>0; i--)
                {
                    if ( rulebuf[i] == ')' )
                        {
                            reverse=i;
                            break;
                        }
                }

            /* Get rule structure,  minus the ( ) */

            for (i=forward+1; i<reverse; i++)
                {
                    snprintf(tmp, sizeof(tmp), "%c", rulebuf[i]);
                    strlcat(rulestr, tmp, sizeof(rulestr));
                }

            /* Get the network information, before the rule */

            for (i=0; i<forward; i++)
                {
                    snprintf(tmp, sizeof(tmp), "%c", rulebuf[i]);
                    strlcat(netstr, tmp, sizeof(netstr));
                }

            /* Assign pointer's to values */

            netstring = netstr;
            rulestring = rulestr;


            /****************************************************************************/
            /* Parse the section _before_ the rule set.  This is stuff like $HOME_NET,  */
            /* $EXTERNAL_NET, etc                                                       */
            /****************************************************************************/

            tokennet = strtok_r(netstring, " ", &saveptrnet);

            while ( tokennet != NULL )
                {

                    if ( netcount == 0 )
                        {
                            if (!strcmp(tokennet, "drop" ))
                                {
                                    rulestruct[counters->rulecount].drop = 1;
                                }
                            else
                                {
                                    rulestruct[counters->rulecount].drop = 0;
                                }
                        }

                    /* Protocol */
                    if ( netcount == 1 )
                        {
                            ip_proto = config->sagan_proto;
                            if (!strcmp(tokennet, "icmp" )) ip_proto = 1;
                            if (!strcmp(tokennet, "tcp"  )) ip_proto = 6;
                            if (!strcmp(tokennet, "udp"  )) ip_proto = 17;
                        }

                    rulestruct[counters->rulecount].ip_proto = ip_proto;

                    /* Source Port */
                    if ( netcount == 3 )
                        {

                            src_port = config->sagan_port;                            /* Set to default */

                            if (strcmp(nettmp, "any")) src_port = atoi(nettmp);       /* If it's _NOT_ "any", set to default */
                            if (Is_Numeric(nettmp)) src_port = atoi(nettmp);          /* If it's a number (see Sagan_Var_To_Value),  then set to that */
                            if ( src_port == 0 ) Sagan_Log(S_ERROR, "[%s, line %d] Invalid source port on line %d in %s", __FILE__, __LINE__, linecount, ruleset);
                            rulestruct[counters->rulecount].src_port = src_port;      /* Set for the rule */
                        }

                    /* Destination Port */
                    if ( netcount == 6 )
                        {

                            dst_port = config->sagan_port;				/* Set to default */

                            if (strcmp(nettmp, "any")) dst_port = atoi(nettmp);	/* If it's _NOT_ "any", set to default */
                            if (Is_Numeric(nettmp)) dst_port = atoi(nettmp);		/* If it's a number (see Sagan_Var_To_Value),  then set to that */
                            if ( dst_port == 0 ) Sagan_Log(S_ERROR, "[%s, line %d] Invalid destination port on line %d in %s", __FILE__, __LINE__, linecount, ruleset);
                            rulestruct[counters->rulecount].dst_port = dst_port;	/* Set for the rule */
                        }


                    tokennet = strtok_r(NULL, " ", &saveptrnet);
                    nettmp = Sagan_Var_To_Value(tokennet); 			/* Convert $VAR to values per line */
                    Remove_Spaces(nettmp);

                    netcount++;
                }


            /*****************************************************************************/
            /* Parse the rule set!                                                       */
            /*****************************************************************************/


            tokenrule = strtok_r(rulestring, ";", &saveptrrule1);

            while ( tokenrule != NULL )
                {

                    rulesplit = strtok_r(tokenrule, ":", &saveptrrule2);
                    Remove_Spaces(rulesplit);

                    /* single flag options.  (nocase, find_port, etc) */

                    if (!strcmp(rulesplit, "parse_port"))
                        {
                            strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_find_port = 1;
                        }

                    if (!strcmp(rulesplit, "parse_proto"))
                        {
                            strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_find_proto = 1;
                        }

                    if (!strcmp(rulesplit, "parse_proto_program"))
                        {
                            strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_find_proto_program = 1;
                        }

                    if (!strcmp(rulesplit, "parse_src_ip"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_find_src_ip = 1;
                            if ( arg == NULL ) Sagan_Log(S_ERROR, "The \"parse_src_ip\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_find_src_pos = atoi(arg);
                        }

                    if (!strcmp(rulesplit, "parse_dst_ip"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_find_dst_ip = 1;
                            if ( arg == NULL ) Sagan_Log(S_ERROR, "The \"parse_dst_ip\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_find_dst_pos = atoi(arg);
                        }

                    /* Non-quoted information (sid, reference, etc) */

                    if (!strcmp(rulesplit, "flowbits"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = Remove_Spaces(strtok_r(arg, ",", &saveptrrule2));

                            if (strcmp(tmptoken, "noalert") && strcmp(tmptoken, "set") && strcmp(tmptoken, "unset") && strcmp(tmptoken, "isset") && strcmp(tmptoken, "isnotset"))
                                {
                                    Sagan_Log(S_ERROR, "Expect 'noalert', 'set', 'unset', 'isnotset' or 'isset' but got '%s' at line %d in %s", tmptoken, linecount, ruleset);
                                }

                            if (!strcmp(tmptoken, "noalert")) rulestruct[counters->rulecount].flowbit_noalert=1;


                            /* SET */

                            if (!strcmp(tmptoken, "set"))
                                {
                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_flag = 1; 				/* We have flowbit in the rule! */
                                    rulestruct[counters->rulecount].flowbit_set_count++;
                                    rulestruct[counters->rulecount].flowbit_type[flowbit_count]  = 1;		/* set */

                                    strlcpy(rulestruct[counters->rulecount].flowbit_name[flowbit_count], tmptoken, sizeof(rulestruct[counters->rulecount].flowbit_name[flowbit_count]));

                                    rulestruct[counters->rulecount].flowbit_timeout[flowbit_count] = atoi(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( rulestruct[counters->rulecount].flowbit_timeout[flowbit_count] == 0 )
                                        Sagan_Log(S_ERROR, "Expected flowbit valid expire time for \"set\" at line %d in %s", linecount, ruleset);

                                    flowbit_count++;
                                    counters->flowbit_total_counter++;

                                }

                            /* UNSET */

                            if (!strcmp(tmptoken, "unset"))
                                {

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected \"direction\" at line %d in %s", linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_direction[flowbit_count] = Sagan_Flowbit_Type(tmptoken, linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_flag = 1;               			/* We have flowbit in the rule! */
                                    rulestruct[counters->rulecount].flowbit_set_count++;
                                    rulestruct[counters->rulecount].flowbit_type[flowbit_count]  = 2;                	/* unset */

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    strlcpy(rulestruct[counters->rulecount].flowbit_name[flowbit_count], tmptoken, sizeof(rulestruct[counters->rulecount].flowbit_name[flowbit_count]));

                                    flowbit_count++;

                                }

                            /* ISSET */

                            if (!strcmp(tmptoken, "isset"))
                                {

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_direction[flowbit_count] = Sagan_Flowbit_Type(tmptoken, linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_flag = 1;               			/* We have flowbit in the rule! */
                                    rulestruct[counters->rulecount].flowbit_type[flowbit_count]  = 3;               	/* isset */

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    strlcpy(rulestruct[counters->rulecount].flowbit_name[flowbit_count], tmptoken, sizeof(rulestruct[counters->rulecount].flowbit_name[flowbit_count]));

                                    /* If we have multiple flowbit conditions (bit1&bit2),
                                     * we alter the flowbit_conditon_count to reflect that.
                                     * |'s are easy.  We just test to see if one of the
                                     * flowbits matched or not!
                                     */

                                    if (Sagan_strstr(rulestruct[counters->rulecount].flowbit_name[flowbit_count], "&"))
                                        {
                                            rulestruct[counters->rulecount].flowbit_condition_count = Sagan_Character_Count(rulestruct[counters->rulecount].flowbit_name[flowbit_count], "&") + 1;
                                        }
                                    else
                                        {
                                            rulestruct[counters->rulecount].flowbit_condition_count++;
                                        }

                                    flowbit_count++;
                                }

                            /* ISNOTSET */

                            if (!strcmp(tmptoken, "isnotset"))
                                {

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_direction[flowbit_count] = Sagan_Flowbit_Type(tmptoken, linecount, ruleset);

                                    rulestruct[counters->rulecount].flowbit_flag = 1;                               	/* We have flowbit in the rule! */
                                    rulestruct[counters->rulecount].flowbit_type[flowbit_count]  = 4;               	/* isnotset */

                                    tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                                    if ( tmptoken == NULL )
                                        Sagan_Log(S_ERROR, "Expected flowbit name at line %d in %s", linecount, ruleset);

                                    strlcpy(rulestruct[counters->rulecount].flowbit_name[flowbit_count], tmptoken, sizeof(rulestruct[counters->rulecount].flowbit_name[flowbit_count]));

                                    /* If we have multiple flowbit conditions (bit1&bit2),
                                     * we alter the flowbit_conditon_count to reflect that.
                                     * |'s are easy.  We just test to see if one of the
                                     * flowbits matched or not!
                                     */

                                    if (Sagan_strstr(rulestruct[counters->rulecount].flowbit_name[flowbit_count], "&"))
                                        {
                                            rulestruct[counters->rulecount].flowbit_condition_count = Sagan_Character_Count(rulestruct[counters->rulecount].flowbit_name[flowbit_count], "&") + 1;
                                        }
                                    else
                                        {
                                            rulestruct[counters->rulecount].flowbit_condition_count++;
                                        }

                                    flowbit_count++;

                                }


                            rulestruct[counters->rulecount].flowbit_count = flowbit_count;

                        }

#ifdef HAVE_LIBGEOIP

                    if (!strcmp(rulesplit, "country_code"))
                        {

                            /* Have the requirements for GeoIP been loaded (Maxmind DB, etc) */

                            if (!config->have_geoip) Sagan_Log(S_ERROR, "[%s, line %d] Rule %s at line %d has GeoIP option,  but Sagan configuration lacks GeoIP!", __FILE__, __LINE__, ruleset, linecount);

                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = strtok_r(arg, " ", &saveptrrule2);

                            if (strcmp(tmptoken, "track"))
                                Sagan_Log(S_ERROR, "[%s, line %d] Expected 'track' in 'country_code' option at line %d in %s", __FILE__, __LINE__, linecount, ruleset);

                            tmptoken = Remove_Spaces(strtok_r(NULL, ",", &saveptrrule2));

                            if (strcmp(tmptoken, "by_src") && strcmp(tmptoken, "by_dst"))
                                Sagan_Log(S_ERROR, "[%s, line %d] Expected 'by_src' or 'by_dst' in 'country_code' option at line %d in %s", __FILE__, __LINE__, linecount, ruleset);

                            if (!strcmp(tmptoken, "by_src")) rulestruct[counters->rulecount].geoip_src_or_dst = 1;
                            if (!strcmp(tmptoken, "by_dst")) rulestruct[counters->rulecount].geoip_src_or_dst = 2;

                            tmptoken = Remove_Spaces(strtok_r(NULL, " ", &saveptrrule2));

                            if (strcmp(tmptoken, "is") && strcmp(tmptoken, "isnot"))
                                Sagan_Log(S_ERROR, "[%s, line %d] Expected 'is' or 'isnot' in 'country_code' option at line %d in %s", __FILE__, __LINE__, linecount, ruleset);

                            if (!strcmp(tmptoken, "isnot")) rulestruct[counters->rulecount].geoip_type = 1;
                            if (!strcmp(tmptoken, "is" )) rulestruct[counters->rulecount].geoip_type = 2;

                            tmptoken = Sagan_Var_To_Value(strtok_r(NULL, ";", &saveptrrule2));           /* Grab country codes */
                            Remove_Spaces(tmptoken);

                            strlcpy(rulestruct[counters->rulecount].geoip_country_codes, tmptoken, sizeof(rulestruct[counters->rulecount].geoip_country_codes));
                            rulestruct[counters->rulecount].geoip_flag = 1;
                        }
#endif

#ifndef HAVE_LIBGEOIP
                    if (!strcmp(rulesplit, "country_code"))
                        {
                            Sagan_Log(S_WARN, "** WARNING: Rule %d of %s has \"country_code:\" tracking but Sagan lacks GeoIP support!", linecount, ruleset);
                            Sagan_Log(S_WARN, "** WARNING: Rebuild Sagan with \"--enable-geoip\" or disable this rule!");
                        }
#endif

                    if (!strcmp(rulesplit, "meta_content"))
                        {

                            if ( meta_content_count > MAX_META_CONTENT )
                                Sagan_Log(S_ERROR, "There is to many \"meta_content\" types in the rule at line %d in %s", linecount, ruleset);

                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = strtok_r(arg, ",", &saveptrrule2);

                            if ( tmptoken == NULL )
                                Sagan_Log(S_ERROR, "[%s, line %d] Expected a meta_content 'helper',  but none was found at line %d in %s", __FILE__, __LINE__, linecount, ruleset);

                            strlcpy(tmp2, Between_Quotes(tmptoken), sizeof(tmp2));
                            strlcpy(rulestruct[counters->rulecount].meta_content_help[meta_content_count], Sagan_Content_Pipe(tmp2, linecount, ruleset), sizeof(rulestruct[counters->rulecount].meta_content_help[meta_content_count]));

                            tmptoken = Sagan_Var_To_Value(strtok_r(NULL, ",", &saveptrrule2));           /* Grab Search data */

                            if ( tmptoken == NULL )
                                Sagan_Log(S_ERROR, "[%s, line %d] Expected some sort of meta_content,  but none was found at line %d in %s", __FILE__, __LINE__, linecount, ruleset);

                            Remove_Spaces(tmptoken);

                            strlcpy(rulestruct[counters->rulecount].meta_content[meta_content_count], tmptoken, sizeof(rulestruct[counters->rulecount].meta_content[meta_content_count]));
                            rulestruct[counters->rulecount].meta_content_flag = 1;

                            tmptoken = strtok_r(NULL, ",", &saveptrrule2);

                            not = strtok_r(arg, "\"", &savenot);
                            if (Sagan_strstr(not, "!")) rulestruct[counters->rulecount].meta_content_not[meta_content_count] = 1;

                            meta_content_count++;
                            rulestruct[counters->rulecount].meta_content_count=meta_content_count;

                        }

                    /* Like "nocase" for content,  but for "meta_nocase".  This is a "single option" but works better here */

                    if (!strcmp(rulesplit, "meta_nocase"))
                        {
                            strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].meta_content_case[meta_content_count-1] = 1;
                            strlcpy(rulestruct[counters->rulecount].meta_content[meta_content_count-1], To_LowerC(rulestruct[counters->rulecount].meta_content[meta_content_count-1]), sizeof(rulestruct[counters->rulecount].meta_content[meta_content_count-1]));
                        }


                    if (!strcmp(rulesplit, "rev" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"rev\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_rev, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_rev));
                        }

                    if (!strcmp(rulesplit, "classtype" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"classtype\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_classtype, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_classtype));

                            for(i=0; i < counters->classcount; i++)
                                {
                                    if (!strcmp(classstruct[i].s_shortname, rulestruct[counters->rulecount].s_classtype))
                                        {
                                            rulestruct[counters->rulecount].s_pri = classstruct[i].s_priority;
                                        }
                                }
                        }

                    if (!strcmp(rulesplit, "program" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"program\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_program, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_program));
                        }

                    if (!strcmp(rulesplit, "reference" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"reference\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_reference[ref_count], Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_reference[ref_count]));
                            rulestruct[counters->rulecount].ref_count=ref_count;
                            ref_count++;
                        }

                    if (!strcmp(rulesplit, "sid" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"sid\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_sid, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_sid));
                        }

                    if (!strcmp(rulesplit, "tag" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"tag\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_tag, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_tag));
                        }

                    if (!strcmp(rulesplit, "facility" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"facility\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_facility, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_facility));
                        }

                    if (!strcmp(rulesplit, "level" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"level\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_level, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].s_level));
                        }


                    if (!strcmp(rulesplit, "pri" ))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"priority\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            Remove_Spaces(arg);
                            rulestruct[counters->rulecount].s_pri = atoi(arg);
                        }

#ifdef HAVE_LIBESMTP

                    if (!strcmp(rulesplit, "email" ))
                        {
                            arg = strtok_r(NULL, " ", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"email\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            if (!strcmp(config->sagan_esmtp_server, "" )) Sagan_Log(S_ERROR, "[%s, line %d] Line %d of %s has the \"email:\" option,  but no SMTP server is specified in the %s", __FILE__, __LINE__, linecount, ruleset, config->sagan_config);
                            strlcpy(rulestruct[counters->rulecount].email, Remove_Spaces(arg), sizeof(rulestruct[counters->rulecount].email));
                            rulestruct[counters->rulecount].email_flag=1;
                            config->sagan_esmtp_flag=1;
                        }
#endif

#ifdef HAVE_LIBLOGNORM

                    if (!strcmp(rulesplit, "normalize" ))
                        {
                            rulestruct[counters->rulecount].normalize = 1;
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"normalize\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            Remove_Spaces(arg);

                            /* Search for a normalize rule that fits the rule set's spec */

                            for (i=0; i < liblognorm_count; i++)
                                {
                                    if (!strcmp(liblognormstruct[i].type, arg ))
                                        {

                                            liblognorm_flag=1;

                                            for (a=0; a < counters->liblognormtoload_count; a++)
                                                {
                                                    if (!strcmp(liblognormstruct[i].type, liblognormtoloadstruct[a].type )) liblognorm_flag=0;
                                                }

                                            if ( liblognorm_flag == 1 )
                                                {
                                                    liblognormtoloadstruct = (liblognorm_toload_struct *) realloc(liblognormtoloadstruct, (counters->liblognormtoload_count+1) * sizeof(liblognorm_toload_struct));
                                                    strlcpy(liblognormtoloadstruct[counters->liblognormtoload_count].type, liblognormstruct[i].type, sizeof(liblognormtoloadstruct[counters->liblognormtoload_count].type));
                                                    strlcpy(liblognormtoloadstruct[counters->liblognormtoload_count].filepath, liblognormstruct[i].filepath, sizeof(liblognormtoloadstruct[counters->liblognormtoload_count].filepath));
                                                    counters->liblognormtoload_count++;
                                                }

                                        }

                                }
                        }

#endif

                    /* Quoted information (content, pcre, msg)  */

                    if (!strcmp(rulesplit, "msg" ))
                        {
                            arg = strtok_r(NULL, ";", &saveptrrule2);
                            strlcpy(tmp2, Between_Quotes(arg), sizeof(tmp2));
                            if (tmp2 == NULL ) Sagan_Log(S_ERROR, "The \"msg\" appears to be incomplete at line %d in %s", linecount, ruleset);
                            strlcpy(rulestruct[counters->rulecount].s_msg, tmp2, sizeof(rulestruct[counters->rulecount].s_msg));
                        }

                    if (!strcmp(rulesplit, "content" ))
                        {
                            if ( content_count > MAX_CONTENT ) Sagan_Log(S_ERROR, "There is to many \"content\" types in the rule at line %d in %s", linecount, ruleset);
                            arg = strtok_r(NULL, ";", &saveptrrule2);
                            strlcpy(tmp2, Between_Quotes(arg), sizeof(tmp2));
                            if (tmp2 == NULL ) Sagan_Log(S_ERROR, "The \"content\" appears to be incomplete at line %d in %s", linecount, ruleset);


                            /* Convert HEX encoded data */

                            strlcpy(final_content, Sagan_Content_Pipe(tmp2, linecount, ruleset), sizeof(final_content));

                            /* For content: ! "something" */

                            not = strtok_r(arg, "\"", &savenot);
                            if (Sagan_strstr(not, "!")) rulestruct[counters->rulecount].content_not[content_count] = 1;

                            strlcpy(rulestruct[counters->rulecount].s_content[content_count], final_content, sizeof(rulestruct[counters->rulecount].s_content[content_count]));
                            final_content[0] = '\0';
                            content_count++;
                            rulestruct[counters->rulecount].content_count=content_count;
                        }

                    /* Single option,  but "nocase" works better here */

                    if (!strcmp(rulesplit, "nocase"))
                        {
                            strtok_r(NULL, ":", &saveptrrule2);
                            rulestruct[counters->rulecount].s_nocase[content_count - 1] = 1;
                            strlcpy(rulestruct[counters->rulecount].s_content[content_count - 1], To_LowerC(rulestruct[counters->rulecount].s_content[content_count - 1]), sizeof(rulestruct[counters->rulecount].s_content[content_count - 1]));
                        }

                    if (!strcmp(rulesplit, "offset"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"offset\" appears to be missing at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_offset[content_count - 1] = atoi(arg);
                        }

                    if (!strcmp(rulesplit, "depth"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"depth\" appears to be missing at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_depth[content_count - 1] = atoi(arg);
                        }


                    if (!strcmp(rulesplit, "distance"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"distance\" appears to be missing at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_distance[content_count - 1] = atoi(arg);
                        }

                    if (!strcmp(rulesplit, "within"))
                        {
                            arg = strtok_r(NULL, ":", &saveptrrule2);
                            if (arg == NULL ) Sagan_Log(S_ERROR, "The \"within\" appears to be missing at line %d in %s", linecount, ruleset);
                            rulestruct[counters->rulecount].s_within[content_count - 1] = atoi(arg);
                        }


                    /* PCRE needs a little extra "work" */

                    if (!strcmp(rulesplit, "pcre" ))
                        {
                            if ( pcre_count > MAX_PCRE ) Sagan_Log(S_ERROR, "There is to many \"pcre\" types in the rule at line %d in %s", linecount, ruleset);
                            arg = strtok_r(NULL, ";", &saveptrrule2);
                            strlcpy(tmp2, Between_Quotes(arg), sizeof(tmp2));
                            if (tmp2 == NULL ) Sagan_Log(S_ERROR, "The \"pcre\" appears to be incomplete at line %d in %s", linecount, ruleset);

                            pcreflag=0;
                            strlcpy(pcrerule, "", sizeof(pcrerule));
                            for ( i = 1; i < strlen(tmp2); i++)
                                {

                                    if ( tmp2[i] == '/' && tmp2[i-1] != '\\' ) pcreflag++;

                                    if ( pcreflag == 0 )
                                        {
                                            snprintf(tmp, sizeof(tmp), "%c", tmp2[i]);
                                            strlcat(pcrerule, tmp, sizeof(pcrerule));
                                        }

                                    /* are we /past/ and at the args? */

                                    if ( pcreflag == 1 )
                                        {
                                            switch(tmp2[i])
                                                {
                                                case 'i':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_CASELESS;
                                                    break;
                                                case 's':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_DOTALL;
                                                    break;
                                                case 'm':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_MULTILINE;
                                                    break;
                                                case 'x':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_EXTENDED;
                                                    break;
                                                case 'A':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_ANCHORED;
                                                    break;
                                                case 'E':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_DOLLAR_ENDONLY;
                                                    break;
                                                case 'G':
                                                    if ( pcreflag == 1 ) pcreoptions |= PCRE_UNGREEDY;
                                                    break;

                                                    /* PCRE options that aren't really used? */

                                                    /*
                                                      case 'f':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_FIRSTLINE; break;
                                                      case 'C':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_AUTO_CALLOUT; break;
                                                      case 'J':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_DUPNAMES; break;
                                                      case 'N':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_NO_AUTO_CAPTURE; break;
                                                      case 'X':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_EXTRA; break;
                                                      case '8':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_UTF8; break;
                                                      case '?':
                                                            if ( pcreflag == 1 ) pcreoptions |= PCRE_NO_UTF8_CHECK; break;
                                                            */

                                                }
                                        }
                                }

                            if ( pcreflag == 0 ) Sagan_Log(S_ERROR, "[%s, line %d] Missing last '/' in pcre: %s at line %d", __FILE__, __LINE__, ruleset, linecount);

                            /* We store the compiled/study results.  This saves us some CPU time during searching - Champ Clark III - 02/01/2011 */

                            rulestruct[counters->rulecount].re_pcre[pcre_count] =  pcre_compile( pcrerule, pcreoptions, &error, &erroffset, NULL );
                            rulestruct[counters->rulecount].pcre_extra[pcre_count] = pcre_study( rulestruct[counters->rulecount].re_pcre[pcre_count], pcreoptions, &error);

                            if (  rulestruct[counters->rulecount].re_pcre[pcre_count]  == NULL )
                                {
                                    Remove_Lock_File();
                                    Sagan_Log(S_ERROR, "[%s, line %d] PCRE failure at %d: %s", __FILE__, __LINE__, erroffset, error);
                                }

                            pcre_count++;
                            rulestruct[counters->rulecount].pcre_count=pcre_count;
                        }


                    /* Snortsam */

                    /* fwsam: src, 24 hours; */

                    if (!strcmp(rulesplit, "fwsam" ))
                        {

                            /* Set some defaults - needs better error checking! */

                            rulestruct[counters->rulecount].fwsam_src_or_dst=1;	/* by src */
                            rulestruct[counters->rulecount].fwsam_seconds = 86400;   /* 1 day */

                            tok_tmp = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = strtok_r(tok_tmp, ",", &saveptrrule2);

                            if (Sagan_strstr(tmptoken, "src")) rulestruct[counters->rulecount].fwsam_src_or_dst=1;
                            if (Sagan_strstr(tmptoken, "dst")) rulestruct[counters->rulecount].fwsam_src_or_dst=2;

                            tmptoken = strtok_r(NULL, ",", &saveptrrule2);
                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);

                            fwsam_time_tmp=atoi(tmptok_tmp);	/* Digit/time */
                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3); /* Type - hour/minute */


                            /* Covers both plural and non-plural (ie - minute/minutes) */

                            if (Sagan_strstr(tmptok_tmp, "second")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp;
                            if (Sagan_strstr(tmptok_tmp, "minute")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60;
                            if (Sagan_strstr(tmptok_tmp, "hour")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60 * 60;
                            if (Sagan_strstr(tmptok_tmp, "day")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60 * 60 * 24;
                            if (Sagan_strstr(tmptok_tmp, "week")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60 * 60 * 24 * 7;
                            if (Sagan_strstr(tmptok_tmp, "month")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60 * 60 * 24 * 7 * 4;
                            if (Sagan_strstr(tmptok_tmp, "year")) rulestruct[counters->rulecount].fwsam_seconds = fwsam_time_tmp * 60 * 60 * 24 * 365;

                        }


                    /* Time based alerting */

                    if (!strcmp(rulesplit, "alert_time"))
                        {

                            rulestruct[counters->rulecount].alert_time_flag = 1;

                            tok_tmp = strtok_r(NULL, ":", &saveptrrule2);
                            strlcpy(tmp2, Sagan_Var_To_Value(tok_tmp), sizeof(tmp2));

                            tmptoken = strtok_r(tmp2, ",", &saveptrrule2);

                            while( tmptoken != NULL )
                                {

                                    if (Sagan_strstr(tmptoken, "days"))
                                        {
                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3);
                                            Remove_Spaces(tmptok_tmp);

                                            if (strlen(tmptok_tmp) > 7 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] To many days (%s) in 'alert_time' in %s at line %d.", __FILE__, __LINE__, tmptok_tmp, ruleset, linecount);
                                                }

                                            strlcpy(alert_time_tmp1, tmptok_tmp, sizeof(alert_time_tmp1));

                                            for (i=0; i<strlen(alert_time_tmp1); i++)
                                                {
                                                    snprintf(tmp, sizeof(tmp), "%c", alert_time_tmp1[i]);
                                                    if (!Is_Numeric(tmp))
                                                        {
                                                            Sagan_Log(S_ERROR, "[%s, line %d] The day '%c' 'alert_time / days' is invalid in %s at line %d.", __FILE__, __LINE__,  alert_time_tmp1[i], ruleset, linecount);
                                                        }

                                                    if ( atoi(tmp) == 0 ) rulestruct[counters->rulecount].alert_days ^= SUNDAY;
                                                    if ( atoi(tmp) == 1 ) rulestruct[counters->rulecount].alert_days ^= MONDAY;
                                                    if ( atoi(tmp) == 2 ) rulestruct[counters->rulecount].alert_days ^= TUESDAY;
                                                    if ( atoi(tmp) == 3 ) rulestruct[counters->rulecount].alert_days ^= WEDNESDAY;
                                                    if ( atoi(tmp) == 4 ) rulestruct[counters->rulecount].alert_days ^= THURSDAY;
                                                    if ( atoi(tmp) == 5 ) rulestruct[counters->rulecount].alert_days ^= FRIDAY;
                                                    if ( atoi(tmp) == 6 ) rulestruct[counters->rulecount].alert_days ^= SATURDAY;

                                                }


                                        }

                                    if (Sagan_strstr(tmptoken, "hours"))
                                        {

                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3);
                                            Remove_Spaces(tmptok_tmp);

                                            if ( strlen(tmptok_tmp) > 9 || strlen(tmptok_tmp) < 9 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] Improper 'alert_time' format in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            snprintf(alert_time_tmp1, sizeof(alert_time_tmp1), "%s", tmptok_tmp);

                                            /* Start hour */
                                            snprintf(alert_time_tmp2, sizeof(alert_time_tmp2), "%c%c", alert_time_tmp1[0], alert_time_tmp1[1]);
                                            rulestruct[counters->rulecount].alert_start_hour = atoi(alert_time_tmp2);

                                            if (!Is_Numeric(alert_time_tmp2))
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' start hour is not nermeric in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            if ( rulestruct[counters->rulecount].alert_start_hour > 23 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' start hour is greater than 23 in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            /* Start minute */
                                            snprintf(alert_time_tmp2, sizeof(alert_time_tmp2), "%c%c", alert_time_tmp1[2], alert_time_tmp1[3]);
                                            rulestruct[counters->rulecount].alert_start_minute = atoi(alert_time_tmp2);

                                            if (!Is_Numeric(alert_time_tmp2))
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' start minute is not nermeric in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            if ( rulestruct[counters->rulecount].alert_start_minute > 59 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' start minute is greater than 59 in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            /* End hour */
                                            snprintf(alert_time_tmp2, sizeof(alert_time_tmp2), "%c%c", alert_time_tmp1[5], alert_time_tmp1[6]);
                                            rulestruct[counters->rulecount].alert_end_hour = atoi(alert_time_tmp2);

                                            if (!Is_Numeric(alert_time_tmp2))
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' end hour is not nermeric in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            if ( rulestruct[counters->rulecount].alert_end_hour > 23 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' end hour is greater than 23 in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }


                                            /* End minute */
                                            snprintf(alert_time_tmp2, sizeof(alert_time_tmp2), "%c%c", alert_time_tmp1[7], alert_time_tmp1[8]);
                                            rulestruct[counters->rulecount].alert_end_minute = atoi(alert_time_tmp2);

                                            if (!Is_Numeric(alert_time_tmp2))
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' end minute is not nermeric in  %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                            if ( rulestruct[counters->rulecount].alert_end_minute > 59 )
                                                {
                                                    Sagan_Log(S_ERROR, "[%s, line %d] 'alert_time' end minute is greater than 59 in %s at line %d.", __FILE__, __LINE__, ruleset, linecount);
                                                }

                                        }

                                    tmptoken = strtok_r(NULL, ",", &saveptrrule2);
                                }


                        }


                    /* Thresholding */

                    if (!strcmp(rulesplit, "threshold" ))
                        {

                            tok_tmp = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = strtok_r(tok_tmp, ",", &saveptrrule2);

                            while( tmptoken != NULL )
                                {

                                    if (Sagan_strstr(tmptoken, "type"))
                                        {
                                            if (Sagan_strstr(tmptoken, "limit")) rulestruct[counters->rulecount].threshold_type = 1;
                                            if (Sagan_strstr(tmptoken, "threshold")) rulestruct[counters->rulecount].threshold_type = 2;
                                        }

                                    if (Sagan_strstr(tmptoken, "track"))
                                        {
                                            if (Sagan_strstr(tmptoken, "by_src")) rulestruct[counters->rulecount].threshold_src_or_dst = 1;
                                            if (Sagan_strstr(tmptoken, "by_dst")) rulestruct[counters->rulecount].threshold_src_or_dst = 2;
                                        }

                                    if (Sagan_strstr(tmptoken, "count"))
                                        {
                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3);
                                            rulestruct[counters->rulecount].threshold_count = atoi(tmptok_tmp);
                                        }

                                    if (Sagan_strstr(tmptoken, "seconds"))
                                        {
                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3 );
                                            rulestruct[counters->rulecount].threshold_seconds = atoi(tmptok_tmp);
                                        }

                                    tmptoken = strtok_r(NULL, ",", &saveptrrule2);
                                }
                        }


                    /* "after"; similar to thresholding,  but the opposite direction */

                    if (!strcmp(rulesplit, "after" ))
                        {
                            tok_tmp = strtok_r(NULL, ":", &saveptrrule2);
                            tmptoken = strtok_r(tok_tmp, ",", &saveptrrule2);

                            while( tmptoken != NULL )
                                {

                                    if (Sagan_strstr(tmptoken, "track"))
                                        {
                                            if (Sagan_strstr(tmptoken, "by_src")) rulestruct[counters->rulecount].after_src_or_dst = 1;
                                            if (Sagan_strstr(tmptoken, "by_dst")) rulestruct[counters->rulecount].after_src_or_dst = 2;
                                        }

                                    if (Sagan_strstr(tmptoken, "count"))
                                        {
                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3);
                                            rulestruct[counters->rulecount].after_count = atoi(tmptok_tmp);
                                        }

                                    if (Sagan_strstr(tmptoken, "seconds"))
                                        {
                                            tmptok_tmp = strtok_r(tmptoken, " ", &saveptrrule3);
                                            tmptok_tmp = strtok_r(NULL, " ", &saveptrrule3 );
                                            rulestruct[counters->rulecount].after_seconds = atoi(tmptok_tmp);
                                        }

                                    tmptoken = strtok_r(NULL, ",", &saveptrrule2);
                                }
                        }


                    tokenrule = strtok_r(NULL, ";", &saveptrrule1);
                }

            /* Some new stuff (normalization) stuff needs to be added */

            if ( debug->debugload )
                {

                    Sagan_Log(S_DEBUG, "---[Rule %s]------------------------------------------------------", rulestruct[counters->rulecount].s_sid);

                    Sagan_Log(S_DEBUG, "= sid: %s", rulestruct[counters->rulecount].s_sid);
                    Sagan_Log(S_DEBUG, "= rev: %s", rulestruct[counters->rulecount].s_rev);
                    Sagan_Log(S_DEBUG, "= msg: %s", rulestruct[counters->rulecount].s_msg);
                    Sagan_Log(S_DEBUG, "= pri: %d", rulestruct[counters->rulecount].s_pri);
                    Sagan_Log(S_DEBUG, "= classtype: %s", rulestruct[counters->rulecount].s_classtype);
                    Sagan_Log(S_DEBUG, "= drop: %d", rulestruct[counters->rulecount].drop);
                    Sagan_Log(S_DEBUG, "= dst_port: %d", rulestruct[counters->rulecount].dst_port);

                    if ( rulestruct[counters->rulecount].s_find_src_ip != 0 )   Sagan_Log(S_DEBUG, "= parse_src_ip");
                    if ( rulestruct[counters->rulecount].s_find_port != 0 ) Sagan_Log(S_DEBUG, "= parse_port");

                    for (i=0; i<content_count; i++)
                        {
                            Sagan_Log(S_DEBUG, "= [%d] content: \"%s\"", i, rulestruct[counters->rulecount].s_content[i]);
                        }

                    for (i=0; i<ref_count; i++)
                        {
                            Sagan_Log(S_DEBUG, "= [%d] reference: \"%s\"", i,  rulestruct[counters->rulecount].s_reference[i]);
                        }
                }

            /* Reset for next rule */

            pcre_count=0;
            content_count=0;
            meta_content_count=0;
            flowbit_count=0;

            netcount=0;
            ref_count=0;
            strlcpy(netstr, "", 1);
            strlcpy(rulestr, "", 1);

            counters->rulecount++;

        } /* end of while loop */

    fclose(rulesfile);
}
