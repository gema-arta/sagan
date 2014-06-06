/*
** Copyright (C) 2009-2014 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2014 Champ Clark III <cclark@quadrantsec.com>
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

/* sagan-meta-content.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include "sagan.h"
#include "sagan-defs.h"

struct _Rule_Struct *rulestruct;

int Sagan_Meta_Content_Search(char *syslog_msg, int rule_position )
{

    char *ptmp = NULL;
    char *tok = NULL;
    char tmp[1024] = { 0 };
    char tmp_search[30] = { 0 }; 	/* Max Domain size is 16 bytes + "Domain: " */

    int return_code = 0;

    strlcpy(tmp, rulestruct[rule_position].meta_content, sizeof(tmp));

    ptmp = strtok_r(tmp, ",", &tok);

    while (ptmp != NULL )
        {

            /* Search for "content help" + "content" */

            snprintf(tmp_search, sizeof(tmp_search), "%s%s ", rulestruct[rule_position].meta_content_help, ptmp);

            if (rulestruct[rule_position].meta_content_nocase == 0 ) { 
	    	if (strcasestr(syslog_msg, tmp_search)) return(TRUE);
		} else { 
		if (strstr(syslog_msg, tmp_search)) return(TRUE);
		}

            ptmp = strtok_r(NULL, ",", &tok);

        }

    return(FALSE);
}

