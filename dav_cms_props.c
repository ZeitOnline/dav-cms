 /**
 *
 * Filespec: $Id$
 * 
 * Filename:      dav_cms_props.c
 * Author:        Ralf Mattes<rm@seid-online.de>
 *
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <httpd.h>
#include <http_log.h>
#include <http_config.h>
#include <http_protocol.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <postgresql/libpq-fe.h>
#include "mod_dav_cms.h"
#include "dav_cms_props.h"

#include <uuid/uuid.h>

/*=================================================================[ TRANSACTION ]==*/

/**
 * Test whether we allready have an open connection to the
 * database. If not, attempt to connect.
 * @param database a pointer to a \c dav_cms_dbh
 * @returns 0 on success or the appropriate error code in case
 * of failure.
 */

static dav_cms_status_t
dav_cms_db_connect (dav_cms_dbh * database)
{
  FILE *dbtrace = NULL;

  if (!database)
    return CMS_FAIL;

  if (!database->dbh)
    {
        if (!database->dsn)
	{
            ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
			"[cms]: No DSN configured");
	  return CMS_FAIL;
	}
      /* try to connect */
      database->dbh = PQconnectdb (database->dsn);
      if (PQstatus (database->dbh) == CONNECTION_BAD)
	{
	  ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "[cms]: Database error '%s'", 
                        PQerrorMessage (database->dbh));
	  PQfinish (database->dbh);
	  database->dbh = NULL;
	  return CMS_FAIL;
	}
      ap_log_error (APLOG_MARK, APLOG_INFO, 0, NULL, 
                    "[cms]: Connected to backend with the following options (%s):", database->dsn);
      
      {
	PQconninfoOption *info, *i;
	info = i = PQconndefaults ();
	while (info->keyword)
	  {
	    ap_log_error (APLOG_MARK, APLOG_INFO, 0, NULL, " '%s' = '%s'", (info->keyword), (info->val));
	    info++;
	  }
	PQconninfoFree (i);
      }
    }
  return CMS_OK;
}

/**
 * Close the connection to the database backend.
 * database. 
 * @param database a pointer to a \c dav_cms_dbh
 * @returns CMS_OK on success or the appropriate error code in case
 * of failure.
 */
static dav_cms_status_t
dav_cms_db_disconnect (dav_cms_dbh * db)
{
  dav_cms_dbh *database;

  database = dbh;

  if (!database)
      return CMS_FAIL;

  if (database->dbh)
    {
        /*FIXME: will this be called too often ? 
         *  PQfinish (database->dbh);
         *  database->dbh = NULL;
         */
        ;
    }
  return CMS_OK;
}

/**
 * Start a transaction in the backend database process.
 * @returns 0 on success or the appropriate error code.
 */
__inline__ static dav_cms_status_t
dav_cms_ensure_transaction (dav_db * db)
{
  PGresult *res;

  /* assertions/preconditions */
  if (!db)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction in weired context (NULL db).");
      return CMS_FAIL;
    }
  if (!db->DTL)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction for '%s' outside of DAV transaction.",
                    db->uri);
      return CMS_FAIL;
    }

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction for '%s' without postgresql connection.",
                    db->uri);
    return CMS_FAIL;
    }

  if (db->PTL == OFF)  /* No backend transaction started yet ... */
    {
      res = PQexec (db->conn, "BEGIN -- property changes");
      if (!res || (PQresultStatus (res) != PGRES_COMMAND_OK))
	{
	  PQclear (res);
	  return CMS_FAIL;
	}
      db->PTL = ON;
      PQclear (res);
      return CMS_OK;
    }
  else           /* backend is allready in transaction */
      return CMS_OK;
}

/**
 * Commit a transaction in the backend database process.
 * @returns 0 on success or the appropriate error code.
 */

__inline__ static dav_cms_status_t
dav_cms_commit (dav_db * db)
{
  PGresult *res;

  if (!db)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction for '%s' in weired context (NULL db)." , 
                    db->uri);
      return CMS_FAIL;
    }

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction for '%s' without postgresql connection.",
                    db->uri);
      return CMS_FAIL;   
    }


  /* Should never happen */
  if ((!db->DTL) && (db->PTL))
    {
    /* We are in a weired state  */
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_commit for '%s' in weired transaction state (%d%d)", 
                    db->uri, db->DTL, db->PTL);
      return CMS_FAIL;
    }

  if (db->PTL)  /* We have an open backend transaction that needs to be commited */  
    {
      res = PQexec (db->conn, "COMMIT -- property changes");
      if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
	{
	  db->PTL = db->DTL = OFF;
	  PQclear (res);
	  return CMS_FAIL;
	}
      PQclear (res);
      db->PTL = db->DTL = OFF;
      return CMS_OK;
    }
  else    /* no backend transaction open */
      {
          db->DTL = OFF;
          return CMS_OK;
      }
}

/**
 * Rollback a transaction in the backend database process.
 * @returns NULL on success or an instance of the appropriate dav error.
 */

__inline__ static dav_error *
dav_cms_rollback (dav_db * db)
{
  PGresult *res;

  if ((!db) || (!db->conn))
      {
        db->DTL = db->PTL = OFF;
        return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                              "Fatal Error: no database connection set up!.");
      }
  
  if ((db->DTL == OFF) && (db->PTL == OFF))
      {return NULL;}

  if ((db->DTL == OFF) && (db->PTL == ON))
    {
    /* We are in a weired state: what can we do. We might have an open
     * backend transaction lingering  ... */
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_rollback for '%s' in weired transaction state",
                    db->uri);
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Fatal Error: weired transaction state during rollback!");;
    }

  if ((db->DTL == ON) &&(db->PTL == ON))
    {	
      res = PQexec (dbh->dbh, "ROLLBACK -- property changes");
      if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
          {
              db->PTL = db->DTL = OFF;
              PQclear (res);
              return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                    "Fatal Error: Could not execute backend rollback:"); 
                                    /* " Database rollback result is %d\n"  */
                                    /* "Error Message '%s'", Pqresultstatus(res) , PQresultErrorMessage(res)); */
          }
      PQclear (res);
      db->DTL = db->PTL = OFF;
      return NULL;
    }	
  else	
      {	
          db->PTL = db->PTL = OFF;
          return NULL;
      }	
}

/*=========================================================[ DAV PROPS CALLBACKS ]==*/

dav_error *
dav_cms_db_open (apr_pool_t * p, const dav_resource * resource, int ro,
		 dav_db ** pdb)
{
  dav_db *db;

  if (!dbh)
    return dav_new_error (p, HTTP_INTERNAL_SERVER_ERROR, 0,
			  "Fatal Error: no database connection set up.");

  /* really open database connection */
  if (dav_cms_db_connect (dbh) != CMS_OK)
    return dav_new_error (p, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_OPENING,
			  "Error connecting to property database");


  db = (dav_db *) apr_pcalloc (p, sizeof (*db));
  db->resource = resource;
  db->pool = p;
  db->conn = dbh->dbh;
  db->cursor = NULL;
  db->pos = db->rows = 0;

  /* NOTE: this is a quick fix to ensure a consistent naming scheme for
   * collection resources. For now we normalize to a version _without_
   * a trailing slash.
   */

  if ((resource->type == DAV_RESOURCE_TYPE_REGULAR) && (resource->collection == 1))
  {
      char* curi = apr_pstrdup(p, resource->uri);
      int   len  = strlen(curi);
      if (len > 1 && curi[len - 1] == '/') {
          curi[len - 1] = '\0';
      }
      db->uri = curi;
  } 
  else
  {
      db->uri = apr_pstrdup(p, resource->uri); 
  } 

  /* NOTE: here we only indicate that we should be in a transaction.
   * The actual (database) transaction is only started the first time
   * we access the database (for both write and delete).
   */
  db->DTL = ON;
  db->PTL = OFF;

  *pdb = db;  
#ifndef NDEBUG
  ap_log_error (APLOG_MARK, APLOG_INFO, 0, NULL,
		"[cms]: Opening database '%s'", resource->uri);
#endif
  return NULL;
}

void
dav_cms_db_close (dav_db * db)
{
#ifndef NDEBUG
    ap_log_error (APLOG_MARK, APLOG_INFO, 0, NULL,
                  "[cms]: Trying to close database for '%s'", db->resource->uri);
#endif

  /*FIXME: is this a good place to commit? */
  if (dav_cms_commit (db) != CMS_OK)
    /* FIXME: how to raise an error ? */
    (void) dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			  "Error commiting transaction in property database\n"
			  "Your operation might not be stored in the backend");
  /* FIXME: Just to keep compiler happy */
  if (0)
    dav_cms_db_disconnect (NULL);
  db->DTL = OFF;
}

dav_error *
dav_cms_db_define_namespaces (dav_db * db, dav_xmlns_info * xi)
{

  PGresult *res;
  char *buffer, *qtempl, *query;
  char *turi;
  size_t tlen, qlen;
  int ntuples, i;

  /* FIXME: to my understanding, we are asked to insert our namespaces
   * (i.e. the ones we manage for the given resource) into the xml
   * namespace info struct. In realiter we want to fetch all
   * namespaces known to us from the database.
   */
  dav_xmlns_add (xi, DAV_CMS_NS_PREFIX, DAV_CMS_NS);

  //return NULL;

  /* Now we select all namespaces defined for the given
   * URI and inset them into the namespace map with generated
   * prefixes.
   */
  ntuples = 0;
  qlen    = 0;

  tlen = strlen (db->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->uri, tlen);
  turi = buffer;
  qlen += tlen;
  
  qtempl = "SELECT DISTINCT namespace FROM facts WHERE uri = '%s'";
  qlen += strlen (qtempl);
  query = (char *) apr_palloc (db->pool, qlen);
  snprintf (query, qlen, qtempl, turi);
  
  /* execute the database query and check return value */
  res = PQexec (db->conn, query);
  if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
    {
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			    "Fatal Error: datbase error during  property storage.");
    }
  ntuples = PQntuples (res);

  for (i = 0; i < ntuples; i++)
    {
      const char *namespace, *prefix;

      namespace =
          apr_pstrdup (xi->pool, (const char *) PQgetvalue (res, i, 0));
      prefix = dav_xmlns_add_uri (xi, namespace);
      ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL,
		    "[Adding ns '%s' as '%s']", namespace, prefix);
    }
  PQclear (res);
  return NULL;
}

/* NEW INTERFACE 

PGresult *PQexecParams(PGconn *conn,
                       const char *command,
                       int nParams,
                       const Oid *paramTypes,
                       const char * const *paramValues,
                       const int *paramLengths,
                       const int *paramFormats,
                       int resultFormat);

*/

dav_error *
dav_cms_db_define_namespaces__new (dav_db * db, dav_xmlns_info * xi)
{

  PGresult *res;
  const char * params[1];
  int ntuples, i;

  /* FIXME: to my understanding, we are asked to insert our namespaces
   * (i.e. the ones we manage for the given resource) into the xml
   * namespace info struct. In realiter we want to fetch all
   * namespaces known to us from the database.
   */
  dav_xmlns_add (xi, DAV_CMS_NS_PREFIX, DAV_CMS_NS);


  /* Now we select all namespaces defined for the given
   * URI and inset them into the namespace map with generated
   * prefixes.
   */
  ntuples   = 0;
  params[0] = db->uri;
  
  /* execute the database query and check return value */

  res = PQexecParams(db->conn, "SELECT DISTINCT namespace FROM facts WHERE uri = $1",
                     1, NULL, params, NULL, NULL, 0); 

  if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
    {
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			    "Fatal Error: datbase error during  property operation.");
    }
  ntuples = PQntuples (res);

  for (i = 0; i < ntuples; i++)
    {
      const char *namespace, *prefix;

      namespace =
          apr_pstrdup (xi->pool, (const char *) PQgetvalue (res, i, 0));
      prefix = dav_xmlns_add_uri (xi, namespace);
#ifndef NDEBUG
      ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL,
		    "[Adding ns '%s' as '%s']", namespace, prefix);
#endif
    }
  PQclear (res);
  return NULL;
}


dav_error *
dav_cms_db_output_value (dav_db * db, const dav_prop_name * name,
			 dav_xmlns_info * xi,
			 apr_text_header * phdr, int *found)
{
  dav_error *err = NULL;
  PGresult *res;
  char *buffer, *qtempl, *query;
  char *turi, *tns, *tname;
  size_t tlen, qlen;
  int ntuples, i;

#ifndef NDEBUG
  ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL,
		"[CMS:DEBUG] Looking for `%s' : `%s'", name->ns,
		name->name);
#endif

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "CLOSED DATABASE");
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_OPENING,
			    "[CMS:FATAL] Trying to access closed database.");
    }

  /* FIXME: why do we have to call this ourself? */
  /* TEST:  It would be convenient if we could call this
   * only once for all properties of _all_ resources within
   * one request (read: a propget/depth=1 on a collection should
   * trigger this only once.
   *
   *
   * err = dav_cms_db_define_namespaces(db, xi);
   * if (err)  return err;
   *
   */

  /* Special cases for namespaces we know we don't touch */
  if ((!strcmp (name->ns, "DAV:")) ||
      (!strcmp (name->ns, "http://apache.org/dav/props/")))
  {
      *found = 0;
      ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL,
		    "[Passing DAV: request]");
      return NULL;
  }
  
  ntuples = 0;
  qlen    = 0;

  /** FIXME: the following code produces wrong URIs for collections 
   * (missing  '/' at the end of the resource name). 
   */
  tlen = strlen (db->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->uri, tlen);
  turi = buffer;
  qlen += tlen;

  tlen = strlen (name->ns);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->ns, tlen);
  tns = buffer;
  qlen += tlen;

  tlen = strlen (name->name);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->name, tlen);
  tname = buffer;
  qlen += tlen;

  qtempl = "SELECT DISTINCT uri, namespace, name, value FROM facts "
    "WHERE uri = '%s' AND namespace ='%s' AND name = '%s'";
  qlen += strlen (qtempl);
  query = (char *) apr_palloc (db->pool, qlen);
  snprintf (query, qlen, qtempl, turi, tns, tname);


  /* execute the database query and check return value */
  res = PQexec (db->conn, query);
  if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
    {
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			    "Fatal Error: datbase error during  property storage.");
    }
  ntuples = PQntuples (res);


  /* Iterate over the result and append the data to the output string */
  for (i = 0; i < ntuples; i++)
    {
        const char *prefix, *uri, *tag, *value;

      uri   = PQgetvalue (res, i, 1);
      tag   = PQgetvalue (res, i, 2);
      value = PQgetvalue (res, i, 3);
      prefix = (char *) dav_xmlns_add_uri (xi, uri);

      /* Ok, this is a quick hack: it seems as if the prefix we get here isn't
       * allways valid ... hmm, we `fix' it be emitting our own xmlns
       * declatation ...
       *
       * buffer = apr_psprintf (db->pool, "<%s:%s xmlns:%s='%s'>%s</%s:%s>",
       *		      prefix, tag,
       *                      prefix, uri,
       *                      value, prefix, tag);
       *                      
       */

      /* Honk: This is a hack we need to ensure proper namespace storage.  Iff
       * we store a structured value (with possibly multiple namesapces) we
       * need to store the full value tree (i.e. including the property name
       * tag) to catch all relevant namespace declarations. Hence we can just
       * output the stored xml.
       */
      if(value && value[0] == '<') {
          buffer = apr_psprintf (db->pool, "%s", value);
      } else {
          buffer = apr_psprintf (db->pool, "<%s:%s>%s</%s:%s>",
                                 prefix, tag,
                                 apr_xml_quote_string (db->pool, value, 0),
                                 prefix, tag);
      }
      apr_text_append (db->pool, phdr, buffer);
    }
  PQclear (res);
  *found = ntuples;
  return NULL;
}

/* 
 * Here we get a chance too have a peek at all namespaces of an incomming
 * WebDAV request. We might need this to create a lookup table to emmit xml:ns
 * declarations during property serialisation.
 */

dav_error *
dav_cms_db_map_namespaces (dav_db * db,
			   const apr_array_header_t * namespaces,
			   dav_namespace_map ** mapping)
{
  int i;
  int *pmap;
  const char **puri;
  dav_namespace_map *m = apr_palloc(db->pool, sizeof(*m));

  m->ns_map = pmap = apr_palloc(db->pool, namespaces->nelts * sizeof(*pmap));

  for (i = 0, puri = (const char **) namespaces->elts;
       i < namespaces->nelts; ++puri, ++i, ++pmap) {
        *pmap = i;
  }
  m->namespaces = namespaces;
  *mapping = m;
  return NULL;
}

dav_error *
dav_cms_db_store (dav_db * db, const dav_prop_name * name,
		  const apr_xml_elem * elem, dav_namespace_map * mapping)
{

  /* FIXME: the following algorithm needs to be implemented:
   * 
   * -# compare the namespace with the hash of namespaces we
   *    are interested in.
   * -# If it is found, store the property in the database,
   *    possibly overriding an older value (stored proc.).
   * -# If not, dispatch to the backend providers store function.
   */
  
  PGresult *res;
  char *buffer;
  char *qtempl, *query;
  char *uri;
  char *value;
  char *turi, *tns, *tname, *tval;
  size_t tlen;
  size_t qlen;
  size_t valsize = 0;
   
  /* NOTE: mod_dav should check for empty namespace itself 
   * Check for invalid or zero-lenght namespace 
   */
  if(name->ns==NULL || strlen(name->ns)==0) {
       return dav_new_error(db->pool, HTTP_BAD_REQUEST, 0,
			    "Fatal Error: NULL namespace not allowed.");
  }

  /* Fastpath: Ignore requests for DAV-properties */
  if (! strcmp (name->ns, "DAV:"))
  {
    return NULL;
  }

  if (!dbh)
    {
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_OPENING,
			    "Fatal Error: no database connection to store property.");
    }

  /* Storage needs explicit backend transactions */
  if (dav_cms_ensure_transaction (db) != CMS_OK)
    return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			  "Error entering transaction context in property database");

  /* ... end debugging */

  /* convert the xml-branch value to its text representation 
   *
   * Iff we have a structured value (we have at least one child) we store a
   * serialized full tree version. We need a full tree (i.e. a tree including
   * the property name tag) here to have an enclosing element to hold all
   * neccessary namespace declarations. 
   * NOTE: this implies some tragic changes to value output as well. See there.
   */

  if (elem->first_child) {    
      apr_xml_quote_elem(db->pool, (apr_xml_elem *) elem);
      apr_xml_to_text (db->pool, elem, APR_XML_X2T_FULL_NS_LANG, 
                       mapping->namespaces, mapping->ns_map, (const char **) &value, &valsize);

  } else {
      apr_xml_to_text (db->pool, elem, APR_XML_X2T_INNER, 
                       NULL, 0, (const char **) &value, &valsize);
  }

  if (value) value[valsize] = (char) 0;

  /* From here on we collect the neccessary tokens, sql-escape them
   * and record the token length to calculate the maximum length of 
   * the query string.
   */
  qlen = 0;
  
  uri  = (char *) db->uri; 
  tlen = strlen (uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, uri, tlen);
  turi = buffer;
  qlen += tlen;
  
  tlen = strlen (name->ns);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->ns, tlen);
  tns = buffer;
  qlen += tlen;

  tlen = strlen (name->name);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->name, tlen);
  tname = buffer;
  qlen += tlen;

  tlen = strlen (value);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, value, tlen);
  tval = buffer;
  qlen += tlen;

  qtempl = "SELECT assert('%s', '%s', '%s', '%s')";
  qlen += strlen (qtempl);
  query = (char *) apr_palloc (db->pool, qlen);
  snprintf (query, qlen, qtempl, turi, tns, tname, tval);

  /* execute the database query and check return value */
  res = PQexec (dbh->dbh, query);
  if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
    {
      PQclear (res);
      return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			    "Fatal Error: datbase error during  property storage. (dav_cms_db_store)");
    }
  PQclear (res);

  return NULL;
}

dav_error *
dav_cms_db_remove (dav_db * db, const dav_prop_name * name)
{
  /* FIXME: how should we react if res.count <> 1? 
   * We also need to do better error checking.
   */

  /* Removal needs expicit backend transactions */
  if (dav_cms_ensure_transaction (db) != CMS_OK)
    return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
			  "Error entering transaction context in property database");
  
  if (dbh)
    {
      PGresult *res;
      char *buffer;
      char *qtempl, *query;
      char *uri;

      char *turi, *tns, *tname;
      size_t tlen;
      size_t qlen;

      qlen = 0;

      uri = (char *) db->uri;
      tlen = strlen (uri);
      buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
      tlen = PQescapeString (buffer, uri, tlen);
      turi = buffer;
      qlen += tlen;

      tlen = strlen (name->ns);
      buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
      tlen = PQescapeString (buffer, name->ns, tlen);
      tns = buffer;
      qlen += tlen;

      tlen = strlen (name->name);
      buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
      tlen = PQescapeString (buffer, name->name, tlen);
      tname = buffer;
      qlen += tlen;

      qtempl =
	"DELETE FROM facts WHERE uri = '%s' AND  namespace = '%s' AND  name = '%s'";
      qlen += strlen (qtempl);
      query = (char *) apr_palloc (db->pool, qlen);
      snprintf (query, qlen, qtempl, turi, tns, tname);

      res = PQexec (dbh->dbh, query);
      /*FIXME: errorchecking here */
      PQclear (res);
    }
  return NULL;
}

int
dav_cms_db_exists (dav_db * db, const dav_prop_name * name)
{
  PGresult *res;
  char *buffer, *qtempl, *query;
  char *turi, *tns, *tname;
  size_t tlen, qlen;
  int exists;

  /* FIXME: is this a joke? No way to signal an error condition? */
  if (!dbh)
    {
      return 0;
    }

  qlen = 0;

  tlen = strlen (db->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->uri, tlen);
  turi = buffer;
  qlen += tlen;

  tlen = strlen (name->ns);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->ns, tlen);
  tns = buffer;
  qlen += tlen;

  tlen = strlen (name->name);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, name->name, tlen);
  tname = buffer;
  qlen += tlen;

  qtempl = "SELECT * FROM facts "
    "WHERE uri = '%s' AND namespace ='%s' AND 'name = '%s'";
  qlen += strlen (qtempl);
  query = (char *) apr_palloc (db->pool, qlen);
  snprintf (query, qlen, qtempl, turi, tns, tname);


  /* execute the database query and check return value */
  res = PQexec (dbh->dbh, query);
  if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
    {
      dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, DAV_ERR_PROP_EXEC,
		     "Fatal Error: datbase error during  property existance check.");
    }
  exists = PQntuples (res);
  ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL, "Existance check: '%d'",
		exists);
  PQclear (res);
  return exists;
}

/**
 * The following two functions (dav_cmd_db_first_name and
 * dav_cms_db_next_name) are called from mod_dav during ALLPROP requests. This
 * is a fastpath to avoid the (slower) sequence "get properties, then get
 * value for each property".
 */

/**
 * @function dav_cms_db_first_name
 *  Read properties for a URI from the database and build an
 *  
 * @param db 
 *   The dav_db struct that holds a hash of all
 *   properties we provide.
 * @param pname
 *   This dav_prop_name struct should be filled with the 
 *   name (and namespace) of the first property we provide.
 * @tip We are supposed to set both namespace and name to NULL
 *   to indicate that we are finished with all properties.
 */
dav_error *
dav_cms_db_first_name (dav_db * db, dav_prop_name * pname)
{
  pname->ns = pname->name = NULL;

  /* If we don't have a result set full of properties, create one
   * and fill it with the properties.
   */
  if (db->cursor == NULL)
    {
      PGresult *res;
      char     *buffer, *qtempl, *query;
      char     *turi;
      size_t tlen, qlen;


      if (!dbh)
	return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			      "Fatal Error: not connected to database.");

      qlen = 0;

      tlen = strlen (db->uri);
      buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
      tlen = PQescapeString (buffer, db->uri, tlen);
      turi = buffer;
      qlen += tlen;

      qtempl = "SELECT DISTINCT namespace, name, value FROM facts " "WHERE uri = '%s'";
      qlen += strlen (qtempl);
      query = (char *) apr_palloc (db->pool, qlen);
      snprintf (query, qlen, qtempl, turi);


      /* execute the database query and check return value */
      res = PQexec (dbh->dbh, query);
      if (!res || PQresultStatus (res) != PGRES_TUPLES_OK)
	{
	  dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			 "Fatal Error: datbase error during  property storage.");
	}
      
      /* return the first property */
      pname->ns   = PQgetvalue (db->cursor, db->pos, 0);
      pname->name = (const char *) PQgetvalue (db->cursor, db->pos, 1);

      /* SIDE NOTE: is this planed at all? Different properties
       * per URI.
       */
      /* move to the first entry */
      db->cursor = res;
      db->rows = PQntuples (res);
      db->pos = 0;
    }


  /* now we start iterating over the entries in the property table.
   * If we are at the end of our result set we set pname->ns and
   * pname->name to NULL to indicate 'no more properties'.
   */
  if (db->pos == db->rows)
    {
      pname->ns = pname->name = NULL;
      PQclear (db->cursor);
      db->cursor = NULL, db->pos = db->rows = 0;
    }
  else
    {
      /*FIXME: do we need to insert namespaces or their prefix here */
      pname->ns = PQgetvalue (db->cursor, db->pos, 0);
      pname->name = (const char *) PQgetvalue (db->cursor, db->pos, 1);

      db->pos++;
    }
  return NULL;
}

dav_error *
dav_cms_db_next_name (dav_db * db, dav_prop_name * pname)
{
    if (db->pos == db->rows)
    {
        pname->ns = pname->name = NULL;
        PQclear (db->cursor);
        /* This indicates 'end-of-props' */
        db->cursor = NULL, db->pos = db->rows = 0;
    }
    else
    {
        /*FIXME: do we need to insert namespaces or their prefix here */
        pname->ns = PQgetvalue (db->cursor, db->pos, 0);
        pname->name = (const char *) PQgetvalue (db->cursor, db->pos, 1);
      
        db->pos++;
    }
    return NULL;
}

/* FIXME: i still don't grasp the concept of this!  Update: I don't have to -
 * this is supposed to store old property values to enable rollback. Since we
 * use database transactions we don't need'em.
 */
dav_error *
dav_cms_db_get_rollback (dav_db * db, const dav_prop_name * name,
			 dav_deadprop_rollback ** prollback)
{
    return NULL;
}

dav_error *
dav_cms_db_apply_rollback (dav_db * db, dav_deadprop_rollback * rollback)
{
    return dav_cms_rollback(db);
    /* FIXME: we might want to return the dav_error here ... */
    /* ... and we might as well just copy-n-paste dav_cms_rollback here! */
}

dav_error *
dav_cms_copy_resource (const dav_resource *src, dav_resource *dst, 
                       int depth, dav_response **response)
{
    ;
}

dav_error *
dav_cms_move_resource (dav_resource *src, dav_resource *dst, dav_response **response)
{
   /*  Here the following shold happen:
     *  - Open transaction 
     *  - Copy properties
     *    If this fails we can rollback the database and return an error.
     *    If this succeeds we call the backend remove callback.
     */
    ;
    /* Iff the resource operation didn't return any errors we can 
     * commit the transaction now. Else we rollback and everything
     * is in it's prior state.
     */
    ;
}


dav_error *
dav_cms_remove_resource (dav_resource *resource, dav_response **response)
{
    /*  Here the following shold happen:
     *  - Open transaction 
     *  - Move properties
     *    If this fails we can rollback the database and return an
     *    error.
     *    If this succeeds we call the backend remove callback.
     */
    ;
    /* Iff the resource operation didn't return any errors we can 
     * commit the transaction now. Else we rollback and everything
     * is in it's prior state.
     */
}


static dav_error *dav_cms_set_option_head(request_rec * r)
{
    apr_table_addn(r->headers_out, "DASL", "<http://namespaces.zeit.de/CMS/SEXY-Search>");
    return NULL;
}

static dav_error *dav_cms_search_resource(request_rec * r, dav_response ** res)
{
    /*
     *  req.proxyreq = apache.PROXYREQ_REVERSE
     *  req.uri = 'http://www.dscpl.com.au' + path
     *  req.filename = 'proxy:%s' % req.uri
     *  req.handler = 'proxy-server'
     */
    
    r->proxyreq = PROXYREQ_REVERSE;
    r->uri      = "http://localhost:9999/";
    r->filename = "proxy:http://localhost:9999/";
    r->handler  = "proxy-server";
    ap_log_error (APLOG_MARK, APLOG_DEBUG, 0, NULL, "%s request for %s", r->method, r->unparsed_uri);
    return dav_new_error(r->pool, HTTP_BAD_REQUEST, 0, "Don't know how to handle SEARCH requests yet!");
    // * FIXME: won't work since we can't proxy at such a late time in the request handling */
}

const dav_hooks_propdb dav_cms_hooks_propdb = {
    dav_cms_db_open,
    dav_cms_db_close,
    dav_cms_db_define_namespaces,
    dav_cms_db_output_value,
    dav_cms_db_map_namespaces,
    dav_cms_db_store,
    dav_cms_db_remove,
    dav_cms_db_exists,
    dav_cms_db_first_name,
    dav_cms_db_next_name,
    dav_cms_db_get_rollback,
    dav_cms_db_apply_rollback,
};

const dav_hooks_search dav_cms_hooks_search = {
    dav_cms_set_option_head,
    dav_cms_search_resource,
    NULL,
};



/* Check whether the given document already has a CMS DocID */
static int
dav_cms_lookup_docID(request_rec *r, const char* resURI, char** docID) {

    const char * params[1];
    PGresult   *result  = NULL;
    int         ntuples = 0;
    int         res;

    params[0] = resURI;

    if(!dbh) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no database");
        return -1;
    }
    if(!dbh->dbh) {
        /* FIXME: we should factor this out into a 
         * call 'getConnection()' that'll either return an existing
         * connection or try to (re)connect.
         */
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database not yet conneced");
        dbh->dbh = PQconnectdb(dbh->dsn);
        if(!dbh->dbh)
        {
            ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no connection");
            return -1;
        }
        else {ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database connection estalished");}
    }
    
    result = PQexecParams(dbh->dbh, 
                          "SELECT value FROM FACTS WHERE uri = $1 AND namespace = 'http://namespaces.zeit.de/CMS/document' AND name = 'uuid'", 
                          1, NULL, params, NULL, NULL, 0);
    if ((!result) || (PQresultStatus(result) != PGRES_TUPLES_OK)) {
        /* FIXME: do something useful here */
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,  "[CMS] No result looking up DocID for %s", resURI);
        return -1;
    }
    
    ntuples = PQntuples(result);
    switch (ntuples) {
    case 0:                     /* No DocID so far */
        *docID = NULL;
        res = OK;
        break;
    case 1:                     /* We already have a DocID */
        *docID = apr_pstrdup(r->pool, PQgetvalue(result, 0, 0));
        res = OK;
        break;
    default:                    /* A document with multiple IDs? */
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,  "[CMS] resource %s has multiple IDs", resURI);
        res -1;
    }

    PQclear(result);
    return res;
}

/** Store a new DocID 
 * We need an explict uri parameter (instead of using r->uri) for
 * copy operations where r points to the copy _source_! 
 */

int
dav_cms_store_docID(request_rec *r, const char* uri, const char *docID) {
    const char * params[2];
    PGresult   *result  = NULL;
    int         ntuples = 0;
    int         res;

    params[0] = uri;
    params[1] = docID;

    if(!dbh) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no database");
        return -1;
    }
    if(!dbh->dbh) {
        /* FIXME: we should factor this out into a 
         * call 'getConnection()' that'll either return an existing
         * connection or try to (re)connect.
         */
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database not yet conneced");
        dbh->dbh = PQconnectdb(dbh->dsn);
        if(!dbh->dbh)
        {
            ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Fatal error: no connection");
            return -1;
        }
        else {ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, "Database connection estalished");}
    }

    result = PQexecParams(dbh->dbh, 
                          "SELECT assert($1, 'http://namespaces.zeit.de/CMS/document', 'uuid', $2)",
                          2, NULL, params, NULL, NULL, 0);
    if ((!result) || (PQresultStatus(result) != PGRES_TUPLES_OK)) { /* FIXME: this leaks! */
        /* FIXME: do something useful here */
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,  "[CMS] Error storing DocID for %s", r->uri);
        res = -1;
    }
    else res = OK;
  
    if (result) PQclear(result);
    return res;
}

const char*
dav_cms_generate_uuid(request_rec *r) {
    uuid_t uuid;
    char   scratch[40];
    
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, scratch);
    return apr_psprintf(r->pool, "{urn:uuid:%s}", scratch);
}


/* This is an Apache fixup hook */
/* FIXME: rename the labels !!! */
int
dav_cms_ensure_uuid(request_rec *r){
    apr_status_t res;
    char   uuid_string[37];
    char  *docID  = NULL;
    char  *resURI = NULL;
    const char *docIDheader = NULL;


    /* Only handle PUT/MKCOL requests */
    if ((M_PUT   != r->method_number) &
        (M_MKCOL != r->method_number)) return DECLINED;
    
    /* Here we need to strip a trailing '/' from collection resource
       URIs.  Ideally we would just change r->uri in place but
       unfortunately mod_dav proper (ha) MKCOL handler expects a
       trailing '/' ... if missing it sends a redirect ...  Rats! */
    {
        resURI = apr_pstrdup(r->pool, r->uri);
        int   len  = strlen(resURI);
        
        if (len > 1 && resURI[len - 1] == '/') {
            resURI[len - 1] = '\0';
        }
    }

    /* Test if this resource already has an UUID */
    /* FIXME: THINK-BUG LURKS HERE!              */
    res = dav_cms_lookup_docID(r, resURI, &docID);
    
    if (res) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,  "[CMS] Error looking up DocID for %s (%d)", r->uri, res);
        return res;        /* FIXME: what to return here? */
    }
    
    docIDheader = apr_table_get(r->headers_in, "Zeit-DocID");

    if (docID) {
        if (docIDheader) {
            if (strcmp(docIDheader, docID)) {
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, 
                             "[CMS] Header docID %s for resource %s conflicts with existing ID %s", 
                             docIDheader, resURI, docID);
                goto conflict;
            }
        }
        goto set_docid;
    }
    
    /*    If a UUID was given check for uniqueness          */
    /*    If unique assign it to the resource an return OK  */
    /*    If no UUID header is given, generate an UUID      */
    if (!docIDheader) { docIDheader = dav_cms_generate_uuid(r); }
    /*    Assign it to the resource and return OK */
    docID = docIDheader;
    res   = dav_cms_store_docID(r, resURI, docID);
    if (res) {
        goto error;
    }
    
    
 set_docid:
    apr_table_setn(r->headers_out, "Zeit-DocID", docID);
    return OK;
    
    
 conflict:
    /* In case the resource already exists ... */
    // set appropriate error message ...
    return HTTP_CONFLICT;       /* or maybe HTTP_PRECONDITION_FAILED */

    
 error:
    return HTTP_INTERNAL_SERVER_ERROR;
}

