/* roxboryd - really small HTTP server
**
** Copyright © 2005 by Michael Richardson <mcr@sandelman.ca>.
**                     All rights reserved.
**
** based upon micro_httpd:
** Copyright © 1999 by Jef Poskanzer <jef@acme.com>. All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>

#define SERVER_NAME "roxboryd " VERSION
#define SERVER_ID "$Id: roxboryd.c,v 1.4 2005/03/26 19:48:34 mcr Exp $" 
#define SERVER_URL "http://cose.gc.ca/projects/roxboryd"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

#define SELF_URL  "http://YOUR.SERVER.HERE:PORT"

/* Forwards. */
static void send_error( int status, char* title, char* extra_header, char* text );
static void send_headers( int status, char* title, char* extra_header, char* mime_type, int length, time_t mod );
static char* get_mime_type( char* name );

int stringcompare(const void *a, const void *b)
{
  char **ca = (char **)a;
  char **cb = (char **)b;

  return strcasecmp(*ca, *cb);
}

void urlescape(char *p, char *q, int len)
{
  static char hexdigit[17]="0123456789ABCDEF";

  while(*p!='\0' && len>0) {
    switch(*p) {

#if 0
    case ' ':
      *q++='+';
      len--;
      break;
#endif
      
    case ' ':
    case '+':
    case '%':
    case '(':
    case ')':
    case '<':
    case '"':
    case '-':
    case 0x27: /* single quote */
    case '#':
      *q++='%';
      *q++=hexdigit[((*p)>>4)&0x0f];
      *q++=hexdigit[(*p)&0x0f];
      len-=3;
      break;
      
    default:
      *q++=*p;
      len--;
      break;
    } 
    
    p++;
  }
  *q='\0';
}

int nibbleval(char nibble) 
{
  if(nibble >= '0' && nibble <= '9') {
    nibble=nibble-'0';
  } else if(nibble >= 'A' && nibble <= 'F') {
    nibble=nibble-'A'+10;
  } else if(nibble >= 'a' && nibble <= 'f') {
    nibble=nibble-'a'+10;
  } else {
    nibble=0;
  }
  return nibble;
}

int hexdecode(char hi, char lo)
{
  hi = nibbleval(hi);
  lo = nibbleval(lo);

  return (hi<<4)+lo;
}

void urldecode(char *from, char *to)
{
  while(*from != '\0' &&
        *from != ' ' &&
        *from != '\n' &&
        *from != '\r') {
    switch(*from) {
    case '+':
      *to++ = ' ';
      from++;
      break;
      
    case '%':
      *to++ = hexdecode(from[1], from[2]);
      from+=3;
      break;

    default:
      *to++=*from++;
      break;
    }
  }
  *to='\0';
}

void send_dir(dirname, sb)
     char *dirname;
     struct stat *sb;
{
  DIR * dirlist;
  struct dirent *di;

  char **f_entries, **oldentries;
  char **d_entries;
  int    max_f_entries = 32;
  int    max_d_entries = 32;
  int    entry_enum;
  int    f_cur_entry = 0;
  int    d_cur_entry = 0;
  char ***entries;
  int  *cur_entry, *max_entries;
  struct stat64 fb;

  f_entries = (char **)malloc(sizeof(char *) * max_f_entries);
  d_entries = (char **)malloc(sizeof(char *) * max_d_entries);

  if((dirlist = opendir(dirname)) == NULL) {
    send_error( 403, "Forbidden", (char*) 0, "Dir is protected." );
    exit(0);
  }

  if(chdir(dirname) < 0) {
    send_error( 500, "Internal Error", (char*) 0, "Config error - couldn't chdir()." );
    exit(0);
  }
 
  send_headers( 200, "Ok", (char*) 0, "text/html", -1, sb->st_mtime );
  (void) printf( "<HTML><HEAD><TITLE>Index of %s</TITLE></HEAD>\n<BODY BGCOLOR=\"#99cc99\"><H4>Index of %s</H4>\n<PRE>\n", dirname, dirname );
  
  while((di = readdir(dirlist)) != NULL) {
    /* skip . files */
    if(di->d_name[0]=='.') continue;

    if(stat64(di->d_name, &fb) < 0) {
      continue;
    }
    if ( S_ISDIR( fb.st_mode ) ) {
      entries   = &d_entries;
      cur_entry = &d_cur_entry;
      max_entries=&max_d_entries;
    } else {
      entries   = &f_entries;
      cur_entry = &f_cur_entry;
      max_entries=&max_f_entries;
    }
      
    if(*cur_entry == *max_entries) {
      *max_entries = *max_entries * 2;
      oldentries = *entries;
      *entries = (char **)realloc(*entries, sizeof(char *) * (*max_entries));
      if(*entries == NULL) {
	*entries = oldentries;
	break;
      }
    }
    (*entries)[(*cur_entry)++] = strdup(di->d_name);
  }
  closedir(dirlist);

  qsort(d_entries, d_cur_entry, sizeof(char *), stringcompare);
  qsort(f_entries, f_cur_entry, sizeof(char *), stringcompare);

  for(entry_enum = 0; entry_enum < d_cur_entry; entry_enum++) {
    char urlbuf[1024];
    char *p,*q;

    p=d_entries[entry_enum];
    q=urlbuf;

    urlescape(p,q, sizeof(urlbuf));
    
    (void)printf("<A HREF=\"%s/\">%s/</A>\n",
		 q, d_entries[entry_enum]);
  }
  printf("<HR>\n");
  
  for(entry_enum = 0; entry_enum < f_cur_entry; entry_enum++) {
    char *ext;
    char urlbuf[1024];
    char *p,*q;

    if((ext = strrchr(f_entries[entry_enum], '.')) != NULL &&
       (((ext[1]=='m' || ext[1]=='M') &&
         (ext[2]=='p' || ext[2]=='P') &&
         (ext[3]=='3' || ext[3]=='3')) ||
        ((ext[1]=='o' || ext[1]=='O') &&
         (ext[2]=='g' || ext[2]=='G') &&
         (ext[3]=='g' || ext[3]=='G'))) &&
       ext[4]=='\0') {
      /* do some special hacks... */
      /* eventually pull the proper title out of ID3 tags, but not yet */
      
      /* just change to .m3u */
      /* terminate string */
      *ext = '\0';

      p=f_entries[entry_enum];
      q=urlbuf;
      
      urlescape(p,q, sizeof(urlbuf));

      (void)printf("<A HREF=\"%s.m3u\">%s</A>\n",
		   q, f_entries[entry_enum]);
    }
    else {
      p=f_entries[entry_enum];
      q=urlbuf;
      
      urlescape(p,q, sizeof(urlbuf));

      (void)printf("<A HREF=\"%s\">%s</A>\n",
		   q, f_entries[entry_enum]);
    }
  }
  
  (void) printf( "</PRE>\n<HR>\n<ADDRESS><A HREF=\"%s\">%s</A></ADDRESS>\n</BODY></HTML>\n", SERVER_URL, SERVER_NAME );

}

int
main( int argc, char** argv )
{
  char line[10000], method[10000], path[10000], protocol[10000], idx[20000], location[20000];
    char * file;
    int len, ich;
    struct stat64 sb;
    FILE* fp;

    if ( argc != 2 )
	send_error( 500, "Internal Error", (char*) 0, "Config error - no dir specified." );
    if ( chdir( argv[1] ) < 0 )
	send_error( 500, "Internal Error", (char*) 0, "Config error - couldn't chdir()." );
    if ( fgets( line, sizeof(line), stdin ) == (char*) 0 )
	send_error( 400, "Bad Request", (char*) 0, "No request found." );
    if ( sscanf( line, "%[^ ] %[^ ] %[^ ]", method, path, protocol ) != 3 )
	send_error( 400, "Bad Request", (char*) 0, "Can't parse request." );
    while ( fgets( line, sizeof(line), stdin ) != (char*) 0 )
	{
	if ( strcmp( line, "\n" ) == 0 || strcmp( line, "\r\n" ) == 0 )
	    break;
	}
    if ( strcasecmp( method, "get" ) != 0 )
	send_error( 501, "Not Implemented", (char*) 0, "That method is not implemented." );

    if ( path[0] != '/' )
	send_error( 400, "Bad Request", (char*) 0, "Bad filename." );

    /* now, URL decode it */
    file=strdup(path+1);
    urldecode(path+1, file);

    if ( file[0] == '\0' )
	file = "./";
    len = strlen( file );
    if ( file[0] == '/' || strcmp( file, ".." ) == 0 || strncmp( file, "../", 3 ) == 0 || strstr( file, "/../" ) != (char*) 0 || strcmp( &(file[len-3]), "/.." ) == 0 )
	send_error( 400, "Bad Request", (char*) 0, "Illegal filename." );

    if ( stat64( file, &sb ) < 0 ) {
      char *ext;
      char *m3ufile;

      if((ext = strrchr(file, '.')) != NULL &&
        ((ext[1]=='m' || ext[1]=='M') &&
         (ext[2]=='3' || ext[2]=='3') &&
         (ext[3]=='u' || ext[3]=='U')) &&
	 ext[4]=='\0') {
	/* do some special hacks... */
	/* eventually pull the proper title out of ID3 tags, but not yet */
	
	m3ufile = strdup(file);
	ext[1]='m';
	ext[2]='p';
	ext[3]='3';
	
	if(stat64(file, &sb) < 0) {
	  ext[1]='M';
	  ext[2]='P';
	  
	  if(stat64(file, &sb) < 0) {
	    ext[1]='o';
	    ext[2]='g';
	    ext[3]='g';
	    if(stat64(file, &sb) < 0) {
	      send_error( 404, "Not Found", file, "MP3 File not found." );
	      exit(0);
            }
	  }
	}

	/* here, we found the MP3 file that corresponds!
	 * we fabricate an M3U file
	 */
	{ 
	  char *fake_contents;
	  int  fake_len;

	  fake_len = strlen(file)*2 + strlen(SELF_URL) + 10;
	  fake_contents = malloc(fake_len);
	  fake_contents[0]='\0';
	  strncat(fake_contents, SELF_URL, fake_len);
	  fake_len -= strlen(SELF_URL);
	  
	  urlescape(file, fake_contents+strlen(SELF_URL), fake_len);
	  strncat(fake_contents, "\015\012", fake_len);
	  
	  send_headers( 200, "Ok", (char*) 0,
			"audio/x-mpegurl", strlen(fake_contents),
			sb.st_mtime );

	  fputs(fake_contents, stdout);

	  free(fake_contents);
	}
	exit(0);
      }
      send_error( 404, "Not Found", file, "File not found." );
      exit(0);
    }

    if ( S_ISDIR( sb.st_mode ) )
	{
	if ( file[len-1] != '/' )
	    {
	    (void) snprintf(
		location, sizeof(location), "Location: %s/", path );
	    send_error( 302, "Found", location, "Directories must end with a slash." );
	    }
	(void) snprintf( idx, sizeof(idx), "%sindex.html", file );
	if ( stat( idx, &sb ) >= 0 )
	    {
	    file = idx;
	    goto do_file;
	    }

	send_dir(file, &sb);
	}
    else
	{
	do_file:
	fp = fopen( file, "r" );
	if ( fp == (FILE*) 0 )
	    send_error( 403, "Forbidden", (char*) 0, "File is protected." );
	send_headers( 200, "Ok", (char*) 0, get_mime_type( file ), sb.st_size, sb.st_mtime );
	while ( ( ich = getc( fp ) ) != EOF )
	    putchar( ich );
	}

    (void) fflush( stdout );
    exit( 0 );
    }

static void
send_error( int status, char* title, char* extra_header, char* text )
    {
    send_headers( status, title, extra_header, "text/html", -1, -1 );
    (void) printf( "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\n<BODY BGCOLOR=\"#cc9999\"><H4>%d %s</H4>\n", status, title, status, title );
    (void) printf( "%s\n", text );
    (void) printf( "<HR>\n<ADDRESS><A HREF=\"%s\">%s</A></ADDRESS>\n</BODY></HTML>\n", SERVER_URL, SERVER_NAME );
    (void) fflush( stdout );
    exit( 1 );
    }

static void
send_headers( int status, char* title, char* extra_header, char* mime_type, int length, time_t mod )
    {
    time_t now;
    char timebuf[100];

    (void) printf( "%s %d %s\r\n", PROTOCOL, status, title );
    (void) printf( "Server: %s\r\n", SERVER_NAME );
    now = time( (time_t*) 0 );
    (void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );
    (void) printf( "Date: %s\r\n", timebuf );
    if ( extra_header != (char*) 0 )
	(void) printf( "%s\r\n", extra_header );
    if ( mime_type != (char*) 0 )
	(void) printf( "Content-Type: %s\r\n", mime_type );
    if ( length >= 0 )
	(void) printf( "Content-Length: %d\r\n", length );
    if ( mod != (time_t) -1 )
	{
	(void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &mod ) );
	(void) printf( "Last-Modified: %s\r\n", timebuf );
	}
    (void) printf( "Connection: close\r\n" );
    (void) printf( "\r\n" );
    }

static char* get_mime_type( char* name )
    {
    char* dot;

    dot = strrchr( name, '.' );
    if ( dot == (char*) 0 )
	return "text/plain; charset=iso-8859-1";
    if ( strcmp( dot, ".html" ) == 0 || strcmp( dot, ".htm" ) == 0 )
	return "text/html; charset=iso-8859-1";
    if ( strcmp( dot, ".jpg" ) == 0 || strcmp( dot, ".jpeg" ) == 0 )
	return "image/jpeg";
    if ( strcmp( dot, ".gif" ) == 0 )
	return "image/gif";
    if ( strcmp( dot, ".png" ) == 0 )
	return "image/png";
    if ( strcmp( dot, ".css" ) == 0 )
	return "text/css";
    if ( strcmp( dot, ".au" ) == 0 )
	return "audio/basic";
    if ( strcmp( dot, ".wav" ) == 0 )
	return "audio/wav";
    if ( strcmp( dot, ".avi" ) == 0 )
	return "video/x-msvideo";
    if ( strcmp( dot, ".mov" ) == 0 || strcmp( dot, ".qt" ) == 0 )
	return "video/quicktime";
    if ( strcmp( dot, ".mpeg" ) == 0 || strcmp( dot, ".mpe" ) == 0 )
	return "video/mpeg";
    if ( strcmp( dot, ".vrml" ) == 0 || strcmp( dot, ".wrl" ) == 0 )
	return "model/vrml";
    if ( strcmp( dot, ".midi" ) == 0 || strcmp( dot, ".mid" ) == 0 )
	return "audio/midi";
    if ( strcmp( dot, ".mp3" ) == 0 )
	return "audio/mpeg";
    if ( strcmp( dot, ".m3u" ) == 0 )
	return "audio/x-mpegurl";
    if ( strcmp( dot, ".pac" ) == 0 )
	return "application/x-ns-proxy-autoconfig";
    return "text/plain; charset=iso-8859-1";
    }
