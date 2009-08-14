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


static char trigger_sql[] =
"INSERT INTO triggers VALUES ($1, $2, $3)";
 
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

/* FIXME: this is only partially refactored code - we want th migrate
 * to the ap_dbd model of database connections*/

static inline ensure_database ()
{
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
      ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database not yet conneced");
      dbh->dbh = PQconnectdb(dbh->dsn);
      if(!dbh->dbh)
	{
	  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no connection");
	  return OK;
	}
      else {ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database connection estalished");}
    }
  return -1;
}

static int dav_cms_log(request_rec *r, const char *method, const char *src, const char *dest)
{
  PGresult   *res;
  const char       * params[2];
  
  
   params[0] = method;
   params[1] = src;
   if(dest)
       {
           params[2] = dest;
       }
   else 
       {
           params[2] = NULL;
       }
  
# ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "-> SQL '%s'", query);
# endif

  res = PQexec (dbh->dbh, "BEGIN -- Logging");
  if (!res || (PQresultStatus (res) != PGRES_COMMAND_OK))
      {
	  PQclear (res);
          ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Could not open logging transaction");
	  return CMS_FAIL;
      }
  
  //res = PQexec(dbh->dbh, query);
  res = PQexecParams(dbh->dbh, trigger_sql,
                     3, NULL, params, NULL, NULL, 0);

  if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
      {
	  PQclear (res);
          ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Could not log operation");
          //FIXME: we need a rollback here ...
	  return CMS_FAIL;
      }
    
  
  res = PQexec (dbh->dbh, "COMMIT -- Logging");
  if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
      {
	  PQclear (res);
          ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Could not commit logging transaction");
	  return CMS_FAIL;
      }
  
  /* NOTE: it would be nice to be able to report back errors during
   * query execution, but, alas, we're too late here, the response is
   * already out!
   */
  return OK;
}

static int dav_cms_log_error(request_rec *r, const char *method, const char *src, const char *dest)
{
  PGresult   *res;
  const char       * params[2];
  
   params[0] = method;
   params[1] = src;
   if(dest)
       {
           params[2] = dest;
       }
   else 
       {
           params[2] = NULL;
       }
   
#ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "ERROR: -> SQL '%s'", query);
# endif
  //  res = PQexec(dbh->dbh, query);
  res = PQexecParams(dbh->dbh, trigger_sql,
                     3, NULL, params, NULL, NULL, 0);

  /* NOTE: it would be nice to be able to report back errors during
   * query execution, but, alas, where too late here, the response is
   * already out!
   */
  return -1;
}


/* FIXME: there is a semantic bug hidden in the following two methods
 * (well, it's actually  one we inherit from braindead mod_dav: _if_ 
 * this operation fails we have no way to roll back and/or notify
 * the client!
 */
static int 
dav_cms_move_props(request_rec *r, const char *src, const char *dest)
{

    PGresult *res;
    const char * params[2];
    int ntuples = 0;
    
    params[0] = src;
    params[1] = dest;
    
    //- BEGIN WORK; 
   
    res = PQexecParams(dbh->dbh, "UPDATE facts SET uri = $2 WHERE uri = $1",
                       2, NULL, params, NULL, NULL, 0); 
    //- COMMIT WORK;
    if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
        {
            /* FIXME: more verbose error messages here */
            return dav_cms_log_error(r, r->method, src, dest);
           
        }
    ntuples = PQntuples (res);

    /* WRONG: we can't subst blindly, otherwise /foo/blub will match /foo/bluber
     * as well, which is wrong!
     * FIXME: what about the canonical name of collection destinations?
     */
    return OK;
}

static int
dav_cms_copy_props(request_rec *r, const char *src, const char *dest)
{
  /* copy all the facts in the facts database ... */

    PGresult *res;
    const char * params[2];
    int ntuples = 0;
        
    params[0] = src;
    params[1] = dest;
    
    //- BEGIN WORK; 
    res = PQexecParams(dbh->dbh, 
                       "INSERT  INTO facts (uri, namespace, name, value) "  
                       "(select $2 as uri, namespace, name, value "
                       "FROM facts WHERE uri = $1)",
                       2, NULL, params, NULL, NULL, 0); 
    //- COMMIT WORK;
    if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
        {
            return dav_cms_log_error(r, r->method, src, dest);
        }
    ntuples = PQntuples (res);

     /* FIXME: what about the canonical name of collection destinations?
     */
    
    return  dav_cms_log(r, r->method, src, dest);
}


/* FIXME: do we need this here ? Yes, this _is_ called from ModDAV */
static int
dav_cms_delete_props(request_rec *r, const char *uri)
{

    PGresult *res;
    const char * params[1];
    int ntuples = 0;
    
    params[0] = uri;
    
    //- BEGIN WORK; 

    /* NOTA BENE: currently we have a hard time to decide iff this
     * resource is a collection (and hence we need to delete the
     * properties of enclosed resources as well) or a simple
     * resource. ModDAV _does_ know, but there's no deletition
     * callback. Currently we need to rely on the fact that a
     * collection uri ends with a '/'.
     **/
    if (uri[strlen(uri)-1] == '/') /* we handle a collection */
        {
            res = PQexecParams(dbh->dbh, 
                               "DELETE FROM facts "  
                               "WHERE uri LIKE ($1||'%')::text",
                               1, NULL, params, NULL, NULL, 0); 
            
        } 
    else 
        {
            res = PQexecParams(dbh->dbh, 
                               "DELETE FROM facts "  
                               "WHERE uri = $1",
                               1, NULL, params, NULL, NULL, 0); 
        }
    //dav_cms_log(r, r->method, uri, "");
    //- COMMIT WORK;
    if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
        {
            return dav_cms_log_error(r, r->method, uri, "");
        }
    ntuples = PQntuples (res);

    /* FIXME: what about the canonical name of collection destinations?
     */
    
    return  OK;

}

/* Dummy implemetation for now */

int dav_cms_monitor(request_rec *r)
{
    int   status;
    const char  *src;     // source URI of a copy/move request
    const char  *dest;    // destination URL (avec netloc) of a copy/move
  
  status = ensure_database ();

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
      /* Ignorable at this point */
    case M_GET:
    case M_OPTIONS:
    case M_PROPFIND:
      break;
    case M_COPY:
        dest = dav_cms_lookup_destination(r);
        if (dest) {
            dav_cms_copy_props(r, src, dest);
        }
        break;
    case M_MOVE:
        dest = dav_cms_lookup_destination(r);
        if (dest) {
            dav_cms_move_props(r, src, dest);
        }
        break;
    case M_DELETE:
        dav_cms_delete_props(r, src);
        break;
    case M_PUT:
    case M_PROPPATCH:
        break;
    default:
      /* what would that be ? 
       * Ok - nosw i know. LOCK, MKCOL, UNLOCK ...
       */
      break;
    }
  dav_cms_log(r, r->method, src, NULL);
  return OK;
}
