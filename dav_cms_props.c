/**
 *
 * Filespec: $Id$
 * 
 * Filename:      dav_cms_props.c
 * Author:        Ralf Mattes<rm@fabula.de>
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
    
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL, 
                    "[cms]: Connected to backend with the following options (%s):", database->dsn);
      {
	PQconninfoOption *info, *i;
	info = i = PQconndefaults ();
	while (info->keyword)
	  {
	    ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL, " '%s' = '%s'", (info->keyword), (info->val));
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
		    "[cms]: dav_cms_ensure_transaction outside of DAV transaction.");
      return CMS_FAIL;
    }

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction without postgresql connection.");
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
		    "[cms]: dav_cms_ensure_transacteion in weired context (NULL db).");
      return CMS_FAIL;
    }

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_ensure_transaction without postgresql connection.");
      return CMS_FAIL;   
    }

  if (!db->DTL)
    {
    /* We are in a weired state  */
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_start_transaction in weired transaction state");
      return CMS_FAIL;
    }

  if (db->PTL)  /* We have an open backend transaction that needs to be commited */  
    {
      res = PQexec (db->conn, "COMMIT -- property changes");
      if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
	{
	  db->PTL = OFF;
	  PQclear (res);
	  return CMS_FAIL;
	}
      PQclear (res);            /* FIXME: don't we need to set PTL to OFF ??? */
      return CMS_OK;
    }
  else          /* no backend transaction open */
    return CMS_OK;
}

/**
 * Rollback a transaction in the backend database process.
 * @returns 0 on success or the appropriate error code.
 */

__inline__ static dav_cms_status_t
dav_cms_rollback (dav_db * db)
{
  PGresult *res;

  if ((!db) || (!db->conn))
    {
      return CMS_FAIL;
    }

  if (!db->DTL)
    {
    /* We are in a weired state  */
      ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,
		    "[cms]: dav_cms_rollback in weired transaction state");
      return CMS_FAIL;
    }

  if ((db->DTL) && (!db->PTL))
    {
      res = PQexec (dbh->dbh, "ROLLBACK -- property changes");
      if (!res || PQresultStatus (res) != PGRES_COMMAND_OK)
	{
	  PQclear (res);
	  return CMS_FAIL;
	}
      PQclear (res);
      return CMS_OK;
    }
  else
    return CMS_OK;
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
  *pdb = db;

  /* NOTE: here we only indicate that we should be in a transaction.
   * The actual transaction is only started the first time we access the
   * database (either for read/write/delete).
   */
  db->DTL = ON;
 
  /* FIXME: we only need to enshure transactions for modifying access to props
   * TEST: removed
   if (dav_cms_ensure_transaction (db) != CMS_OK)
   return dav_new_error (p, HTTP_INTERNAL_SERVER_ERROR, 0,
   "Error entering transaction context in property database");
  ****/

#ifndef NDEBUG
  ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
		"[cms]: Opening database '%s'", resource->uri);
#endif
  return NULL;
}

void
dav_cms_db_close (dav_db * db)
{
  const dav_resource *resource = NULL;

#ifndef NDEBUG
  resource = db->resource;
  ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
		"[cms]: Closing database for '%s'", resource->uri);
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

  tlen = strlen (db->resource->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->resource->uri, tlen);
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
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
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
  char * params[1];
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
  params[0] = db->resource->uri;
  
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
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
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
  ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
		"[CMS:DEBUG] Looking for `%s' : `%s'", name->ns,
		name->name);
#endif

  if (!db->conn)
    {
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL, "CLOSED DATABASE");
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
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
		    "[Passing DAV: request]");
      return NULL;
  }
  
  ntuples = 0;
  qlen      = 0;

  tlen = strlen (db->resource->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->resource->uri, tlen);
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

  qtempl = "SELECT uri, namespace, name, value FROM facts "
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
       * we store a structured value /with possibly multiple namesapces) we
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
      ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
		    "[URI: '%s 'Prefix '%s'] %s", uri, prefix, buffer);
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

        ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL,
                      "[Namespace %d URI: '%s']", i, *puri);
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

  uri  = (char *) db->resource->uri;
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
			    "Fatal Error: datbase error during  property storage.");
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

      uri = (char *) db->resource->uri;
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

  tlen = strlen (db->resource->uri);
  buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
  tlen = PQescapeString (buffer, db->resource->uri, tlen);
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
  ap_log_error (APLOG_MARK, APLOG_WARNING, 0, NULL, "Existance check: '%d'",
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
      char *buffer, *qtempl, *query;
      char *turi;
      size_t tlen, qlen;


      if (!dbh)
	return dav_new_error (db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			      "Fatal Error: not connected to database.");

      qlen = 0;

      tlen = strlen (db->resource->uri);
      buffer = (char *) apr_palloc (db->pool, 2 * (tlen + 1));
      tlen = PQescapeString (buffer, db->resource->uri, tlen);
      turi = buffer;
      qlen += tlen;

      qtempl = "SELECT namespace, name, value FROM facts " "WHERE uri = '%s'";
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
    dav_cms_rollback(db);
    db->DTL = OFF;
    return NULL;
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
