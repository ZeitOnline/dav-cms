/**
 * @package dav_cms
 * Filespec: $Id$
 */

#include <httpd.h>
#include <http_log.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_request.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <postgresql/libpq-fe.h>
#include "mod_dav_cms.h"
#include "dav_cms_monitor.h"


static char trigger1_sql[] =
"INSERT INTO triggers VALUES ('%s', '%s', '%s')";

static char trigger2_sql[] =
"INSERT INTO triggers VALUES ('%s', '%s')";


static char *
dav_cms_lookup_destination(request_rec *r)
{
  const char *dest;
  char       *uri;

  dav_lookup_result res;
  
  if (!(dest = (char *) apr_table_get(r->headers_in, "Destination")))
    return NULL;
  
  res = dav_lookup_uri(dest, r, 1);
  if((res.rnew->status < 200) || (res.rnew->status >= 300))
    return NULL;
    
  uri =  apr_pstrdup(r->pool, res.rnew->uri);
  ap_destroy_sub_req(res.rnew);
  return uri;
}

static int dav_cms_log(request_rec *r, const char *method, const char *src, const char *dest)
{
  PGresult   *res;
  char       *query;
  size_t      src_len, dest_len, query_len;
   
  if(!dbh)
    {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no database");
      return OK;
    }
  if(!dbh->dbh)
    {
      /* FIXME: we should factor this out into a 
       * call 'getConnection()' that'll either return an existing
       * connection or try to (re)connect.
       */
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "no connection");
      dbh->dbh = PQconnectdb(dbh->dsn);
      if(!dbh->dbh)
	{
	  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no connection");
	  return OK;
	}
    }

  query_len = src_len = dest_len = 0;
  src_len   = strlen(src);
 
  if(dest)
    {
      query_len  += strlen(method);
      query_len  += strlen(src);
      query_len  += strlen(dest);
      query_len  += strlen(trigger1_sql);
      query = (char *) apr_palloc(r->pool, 2 * query_len);
      snprintf(query, query_len, trigger1_sql, r->method, src, dest);
    }
  else 
    {
      query_len  += strlen(method);
      query_len  += strlen(src);
      query_len  += strlen(trigger2_sql);
      query = (char *) apr_palloc(r->pool, 2 * query_len);
      snprintf(query, query_len, trigger2_sql, r->method, src);
    }
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "\tSQL '%s'\n", query);
  res   = PQexec(dbh->dbh, query);
  return OK;
}


/* FIXME: there is a semantic bug hidden in the following two methods
 * (well, it's actually  one we inherit from braindead mod_dav: _if_ 
 * this operation fails we have no way to roll back and/or notify
 * the client!
 */
static int 
dav_cms_move_props(request_rec *r, const char *src, const char *dest)
{

  /* update all uri's in the facts database
   *
   *- BEGIN WORK;
   *- UPDATE facts SET uri = subst(uri , 'src', 'dest') WHERE URI LIKE 'src%';
   *- COMMIT WORK;
   * WRONG: we can't subst blindly, otherwise /foo/blub will match /foo/bluber
   * as well, which is wrong!
   * FIXME: what about the canonical name of collection destinations?
   */
  return dav_cms_log(r, r->method, src, dest);
}

static int
dav_cms_copy_props(request_rec *r, const char *src, const char *dest)
{
  /* copy all the facts in the facts database ... */
  return  dav_cms_log(r, r->method, src, dest);
}

/* FIXME: do we need this here ? */
static int
dav_cms_delete_props(request_rec *r, const char *uri)
{
  
  return OK;
}

/* Dummy implemetation for now */

int dav_cms_monitor(request_rec *r)
{
  const char  *src;     // source URI of a copy/move request
  const char  *dest;    // destination URL (avec netloc) of a copy/move


  src  = r->uri;
  dest = "";

  /* Currently we only deal with successfull requests (200 <= STATUS < 300), 
   * i.e. only if a request did run successfuly there's a chance that
   * either content or meta-inforamtion did actually change (short of the 
   * bugs in mod_dav that might leave the server in  an inconsistent state 
   * caused by some errors).
   */
  if ((r->status < 200) || (r->status >= 300))
    return OK;
  
  switch(r->method_number)
    {
      /* Ignorable */
    case M_GET:
    case M_OPTIONS:
    case M_PROPFIND:
      break;
    case M_COPY:
      dest = dav_cms_lookup_destination(r);
      dav_cms_copy_props(r, src, dest);
    case M_MOVE:
      dest = dav_cms_lookup_destination(r);
      dav_cms_move_props(r, src, dest);
      break;
    case M_PUT:
    case M_PROPPATCH:
    case M_DELETE:
      dav_cms_delete_props(r, src);
    default:
      /* what would that be ? */
       dav_cms_log(r, r->method, src, NULL);
      break;
    }
  return OK;
}
