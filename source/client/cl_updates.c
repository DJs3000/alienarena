/*
Copyright (C) 2011 COR Entertainment, LLC.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include "curl/curl.h"

#include "client.h"

extern cvar_t  *cl_latest_game_version_url;

static char *versionstr = NULL;
static size_t versionstr_sz = 0;

struct dotted_triple
{
	unsigned long major;
	unsigned long minor;
	unsigned long point;
};
static struct dotted_triple this_version;
static struct dotted_triple latest_version;
static char update_notice[256];


/**
 *
 * valid dotted version numbers
 *   double  <0..99>.<1..99> (.point is 0)
 *   triple  <0..99>.<1..99>.<1..99>
 *
 * @param vstring      string from server
 * @param version_out  parsed dotted triple output
 * @return             true if valid, false otherwise
 */
static qboolean parse_version( const char* vstring, struct dotted_triple *version_out )
{
	char* pch_start;
	char* pch_end;
	qboolean valid_version = false;
	unsigned long major;
	unsigned long minor;
	unsigned long point;
	
	pch_start = (char*)vstring;
	if ( isdigit( *pch_start ) )
	{
		major = strtoul( vstring, &pch_end, 10 );
		if ( major <= 99UL && *pch_end == '.' )
		{
			pch_start = pch_end + 1;
			if ( isdigit( *pch_start ) )
			{
				minor = strtoul( pch_start, &pch_end, 10 );
				if ( minor >= 1 && minor <= 99UL )
				{
					if ( *pch_end == '.' )
					{
						pch_start = pch_end + 1;
						if ( isdigit( *pch_start ) )
						{
							point = strtoul( pch_start, &pch_end, 10 );
							if ( point >= 1UL && point <= 99UL && *pch_end == '\0')
							{ /* valid x.y.z */
								version_out->major = major;
								version_out->minor = minor;
								version_out->point = point;
								valid_version = true;
							}
						}
					}
					else if ( *pch_end == '\0' )
					{ /* valid x.y */
						version_out->major = major;
						version_out->minor = minor;
						version_out->point = 0UL;
						valid_version = true;
					}
				}
			}
		}
	}
	
	return valid_version;
}

/**
 *  generate a version update notice, or nul the string
 *  see VersionUpdateNotice() below
 * 
 * @param vstring  the latest version from the server
 *
 */ 
static void update_version( const char* vstring )
{
	qboolean valid_version;
	qboolean update_message = false;
	
	valid_version = parse_version( vstring, &latest_version );
	if ( valid_version )
	{ /* valid from server */
		valid_version = parse_version( VERSION, &this_version );
		if ( valid_version )
		{ /* local should always be valid */
			if ( this_version.major < latest_version.major )
			{
				update_message = true;
			}
			else if ( this_version.major == latest_version.major 
				&& this_version.minor < latest_version.minor )
			{
				update_message = true;
			}
			else if ( this_version.major == latest_version.major 
				&& this_version.minor == latest_version.minor 
				&& this_version.point < latest_version.point )
			{
				update_message = true;
			}
		}
	}
	if ( update_message )
	{
		char this_string[16];
		char latest_string[16];

		if ( latest_version.point == 0UL )
		{ /* x.y */
			Com_sprintf( latest_string, sizeof(latest_string), "%d.%d", 
				latest_version.major, latest_version.minor );
		}
		else
		{ /* x.y.z */
			Com_sprintf( latest_string, sizeof(latest_string), "%d.%d.%d", 
				latest_version.major, latest_version.minor, latest_version.point );
		}
		if ( this_version.point == 0UL )
		{ /* x.y */
			Com_sprintf( this_string, sizeof(this_string), "%d.%d", 
				this_version.major, this_version.minor );
		}
		else
		{ /* x.y.z */
			Com_sprintf( this_string, sizeof(this_string), "%d.%d.%d", 
				this_version.major, this_version.minor, this_version.point );
		}
		Com_sprintf( update_notice, sizeof(update_notice),
			 	"version %s available (%s currently installed)", 
			 	latest_string, this_string );
	}
	else
	{ /* not available */
		update_notice[0] = '\0';
	}
}

static char* extend_versionstr ( size_t bytecount )
{
	char *new_versionstr;
	size_t cur_sz = versionstr_sz;
	if ( cur_sz ){
	    versionstr_sz += bytecount;
	    new_versionstr = realloc ( versionstr, versionstr_sz );
	    if (new_versionstr == NULL) {
	    	free (versionstr);
	    	Com_Printf ("WARN: SYSTEM MEMORY EXHAUSTION!\n");
	    	versionstr_sz = 0;
	    	versionstr = NULL;
	        return NULL;
	    }
	    versionstr = new_versionstr;
	    return versionstr+cur_sz;
	}
	versionstr_sz = bytecount;
	versionstr = malloc ( versionstr_sz );
	return versionstr;
}

static size_t write_data(const void *buffer, size_t size, size_t nmemb, void *userp)
{
	char *buffer_pos;
	size_t bytecount = size*nmemb;

	buffer_pos = extend_versionstr ( bytecount );
	if (!buffer_pos)
	    return 0;

	memcpy ( buffer_pos, buffer, bytecount );
	buffer_pos[bytecount] = 0;

	return bytecount;
}

void getLatestGameVersion( void )
{
	char url[128];
	CURL* easyhandle;

	easyhandle = curl_easy_init() ;

    versionstr_sz = 0;

	Com_sprintf(url, sizeof(url), "%s", cl_latest_game_version_url->string);

	if (curl_easy_setopt( easyhandle, CURLOPT_URL, url ) != CURLE_OK) return ;

	// time out in 5s
	if (curl_easy_setopt(easyhandle, CURLOPT_CONNECTTIMEOUT, 5) != CURLE_OK) return ;

	if (curl_easy_setopt( easyhandle, CURLOPT_WRITEFUNCTION, write_data ) != CURLE_OK) return ;

	if (curl_easy_perform( easyhandle ) != CURLE_OK) return;

	(void)curl_easy_cleanup( easyhandle );

	if ( versionstr_sz > 0 )
	{
		if ( versionstr != NULL )
		{ 
			update_version( versionstr );
	        free ( versionstr );
	        versionstr = NULL;
        }
        versionstr_sz = 0;
	}
	else if ( versionstr != NULL )
	{ // error: size <= 0
		free( versionstr );
		versionstr_sz = 0;
		versionstr = NULL;
	}
}

/**
 *  
 * @returns NULL if program is latest version, pointer to update notice otherwise
 */
char* VersionUpdateNotice( void )
{
	if ( update_notice[0] == '\0' )
		return NULL;
	else
		return update_notice;		
}
