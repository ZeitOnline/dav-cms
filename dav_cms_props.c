/* Filespec: $Id$
/**
 * @package dav_cms
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


//########################################################################[ UTILS ]##

/**
 * Test whether we allready have an open connection to the
 * database. If not, attempt to connect.
 * @returns 0 on success or the appropriate error code in case
 * of failure.
 */

static dav_cms_status_t
dav_cms_db_connect(dav_cms_dbh *database)
{
  if (!database)
    return CMS_FAIL;

  if (!database->dbh)
    {
      if(!database->dsn)
	{
	  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, 
		       "[cms]: No DSN configured");
	  return CMS_FAIL;
	}
      database->dbh = PQconnectdb(database->dsn);
      if (PQstatus(database->dbh) == CONNECTION_BAD)
	{
	  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, 
		       "[cms]: Database error '%s'", PQerrorMessage(database->dbh));
	  PQfinish(database->dbh);
	  /* FIXME: set connection to NULL */
	  return CMS_FAIL;
	}
      /* set module db handle from dbconn */
      // = dbh;
    }
  return CMS_OK;
}

static dav_cms_status_t
dav_cms_db_disconnect(dav_cms_dbh *db)
{
  dav_cms_dbh *database;

  database = dbh;

  if(!database)
    return CMS_FAIL;

  if(database->dbh)
    {
      //PQfinish(database->dbh);
    }
  return 0;
}


static dav_cms_status_t
dav_cms_start_transaction(void)
{
  PGresult *res;
  
  if(!dbh->dbh)
    return CMS_FAIL;

  res = PQexec(dbh->dbh, "BEGIN");
  if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
    {
      PQclear(res);
      return CMS_FAIL;
    }
  PQclear(res);
  return CMS_OK;
}

static dav_cms_status_t
dav_cms_commit(void)
{
  PGresult *res;
  
  if(!dbh->dbh)
    return CMS_FAIL;

  res = PQexec(dbh->dbh, "COMMIT");
  if (!res || PQresultStatus(res) != PGRES_COMMAND_OK)
    {
      PQclear(res);
      return CMS_FAIL;
    }
  PQclear(res);
  return CMS_OK;
}

static dav_cms_status_t
dav_cms_rollback(void)
{
  return CMS_OK;
}


//##########################################################[ DAV PROPS CALLBACKS ]##

dav_error *
dav_cms_db_open(apr_pool_t *p, const dav_resource *resource, int ro, dav_db **pdb)
{
  dav_db           *db;
  
  if(!dbh)
    return dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Fatal Error: no database connection set up.");

  /* really open database connection */
  if(dav_cms_db_connect(dbh) != CMS_OK)
    return dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Error connecting to property database");


  db = (dav_db *)apr_pcalloc(p, sizeof(*db));
  db->resource = resource;
  db->pool     = p;
  db->conn     = dbh->dbh;
  db->cursor   = NULL;
  db->pos = db->rows = 0;
  *pdb = db;

  /* FIXME: in reality we would either open a connection to the
   * postgres backend or begin a transaction on an open connection.
   */
  if (dav_cms_start_transaction() != CMS_OK)
    return dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Error entering transaction context in property database");


#ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL,  "[cms]: Opening database '%s'\n", resource->uri);
#endif
  return NULL;
}
    
void
dav_cms_db_close(dav_db *db)
{
  const dav_resource *resource = NULL;

#ifndef NDEBUG
  resource = db->resource;
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL,  "[cms]: Closing database for '%s'\n", resource->uri);
#endif

  /*FIXME: is this a good place to commit? */
 if (dav_cms_commit() != CMS_OK)
   /* FIXME: how to raise an error ? */
   (void) dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
		 "Error commiting transaction in property database\n"
		 "Your operation might not be stored in the backend");
 /* FIXME: Just to keep compiler happy*/
 if(0) dav_cms_rollback();
 if(0) dav_cms_db_disconnect(NULL);
}

dav_error *
dav_cms_db_define_namespaces(dav_db *db, dav_xmlns_info *xi)
{

  PGresult   *res;  
  char       *buffer, *qtempl, *query;
  char       *turi;
  size_t      tlen, qlen;
  int         ntuples, i;

  /* FIXME: to my understanding, we are asked to insert our namespaces
   * (i.e. the ones we manage for the given resource) into the xml
   * namespace info struct. In realiter we want to fetch all
   * namespaces known to us from the database.
   */
  dav_xmlns_add(xi, DAV_CMS_NS_PREFIX, DAV_CMS_NS);
  
  /* Now we select all namespaces defined for the given
   * URI and inset them into the namespace map with generated
   * prefixes.
   */
  ntuples = 0;
  qlen    = 0;
  
  tlen   = strlen(db->resource->uri);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, db->resource->uri, tlen);
  turi   = buffer;
  qlen  += tlen;
  
  qtempl = "SELECT namespace FROM facts WHERE uri = '%s'"; 
  qlen  += strlen(qtempl);
  query = (char *) apr_palloc(db->pool, qlen);
  snprintf(query, qlen, qtempl, turi);
  
  /* execute the database query and check return value */
  res   = PQexec(db->conn, query);
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
      return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			   "Fatal Error: datbase error during  property storage.");	  
    }
  ntuples = PQntuples(res);
  for(i = 0; i < ntuples; i++)
    {
      char *namespace, *prefix;
      
      namespace = apr_pstrdup(xi->pool, (const char *) PQgetvalue(res, i, 0));
      prefix    = apr_psprintf(db->pool, "CMS%d", i);
      dav_xmlns_add(xi, prefix, namespace);
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "[Adding ns '%s' as '%s']\n", namespace, prefix);
    }
  dav_xmlns_add(xi, "ZEIT", "http://namespaces.zeit.de/blub");
  PQclear(res);
  return NULL;
}

dav_error *
dav_cms_db_output_value(dav_db *db, const dav_prop_name *name,
			dav_xmlns_info  *xi,
			apr_text_header *phdr, 
			int *found)
{
  dav_error  *err;
  PGresult   *res;  
  char       *buffer, *qtempl, *query;
  char       *turi, *tns, *tname;
  size_t      tlen, qlen;
  int         ntuples, i;

#ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
	       "[CMS:DEBUG]\tLooking for `%s' : `%s'\n", name->ns, name->name);
#endif

  if(!db->conn) 
    {
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "CLOSED DATABASE\n");
      return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			   "[CMS:FATAL]\tTrying to access closed database.");
    }
  
  /* FIXME: why do we have to call this ourself? */
  /* TEST:  It would be convenient if we could call this
   * only once for all properties of _all_ resources within
   * one request (read: a propget/depth=1 on a collection should
   * trigger this only once.
   */
  err = dav_cms_db_define_namespaces(db, xi);
  if(err) return err;

  ntuples = 0;
  qlen    = 0;
  
  tlen   = strlen(db->resource->uri);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, db->resource->uri, tlen);
  turi   = buffer;      
  qlen  += tlen;

  tlen   = strlen(name->ns);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, name->ns, tlen);
  tns    = buffer;
  qlen  += tlen;

  tlen   = strlen(name->name);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, name->name, tlen);
  tname  = buffer; 
  qlen  += tlen;

  qtempl = "SELECT uri, namespace, name, value FROM facts "
    "WHERE uri = '%s' AND namespace ='%s' AND name = '%s'"; 
  qlen  += strlen(qtempl);
  query = (char *) apr_palloc(db->pool, qlen);
  snprintf(query, qlen, qtempl, turi, tns, tname);
      

  /* execute the database query and check return value */
  res   = PQexec(db->conn, query);
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
      return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			   "Fatal Error: datbase error during  property storage.");	  
    }
  ntuples = PQntuples(res);
  
  
  /* Iterate over the result and append the data to the output string */
  for(i = 0; i < ntuples; i++)
    {
      char *prefix, *uri, *tag;
	
      uri = PQgetvalue(res, i, 1);
      tag = PQgetvalue(res, i, 2);
      prefix = (char *)dav_xmlns_get_prefix(xi, uri);
      prefix = (char *) apr_hash_get(xi->uri_prefix, uri, -1);
      //prefix = prefix ? prefix : "ZEIT";
      buffer = apr_psprintf(db->pool,"<%s:%s>%s</%s:%s>",
			    prefix,
			    tag,
			    PQgetvalue(res, i, 3),
			    prefix,
			    tag);
      apr_text_append(db->pool, phdr, buffer);
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
		   "[URI: '%s' Prefix '%s']\n", uri, prefix);
    }
  PQclear(res);
  *found = ntuples;
  return NULL;
}

dav_error *
dav_cms_db_map_namespaces(dav_db *db,
			  const apr_array_header_t *namespaces,
			  dav_namespace_map **mapping)
{
  return NULL;
}

dav_error * 
dav_cms_db_store(dav_db *db, const dav_prop_name *name,
	      const apr_xml_elem *elem,
	      dav_namespace_map *mapping)
{
  /* FIXME: we need to check that property with the given name
   * and namespace doesn't allready exist for this URI,
   */

  /* FIXME: the following algorythm needs to be implemented:
   * 
   * -# compare the namespace with the hash of namespaces we
   *    are interested in.
   * -# If it is found, store the property in the database,
   *    possibly overriding an older value (stored proc.).
   * -# If not, dispatch to the backend providers store function.
   */
      PGresult *res;
      char     *buffer;
      char     *qtempl, *query;
      char     *uri;
      char     *value;
      char     *turi, *tns, *tname, *tval; 
      size_t    tlen;
      size_t    qlen;
      size_t    valsize = 0;
      
      if(!dbh)
	{
	  return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			"Fatal Error: no database connection to store property.");	  
	}
      
      /* convert the xml-branch value to its text representation */
      apr_xml_to_text(db->pool, elem, APR_XML_X2T_INNER, NULL, 0,
		      (const char **) &value, &valsize);
      if(value)
	value[valsize] = (char) 0;
      
      /* From here on we collect the neccessary tokens, sql-escape them
       * and record the token length to calculate the maximum length of 
       * the query string.
       */
      qlen   = 0;
      
      uri    = (char *) db->resource->uri;
      tlen   = strlen(uri);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, uri, tlen);
      turi   = buffer;      
      qlen  += tlen;

      tlen   = strlen(name->ns);
      /* FIXME: mod_dav should check for empty namespace itself */
      if(name->ns[0] == 0)
	{
	  return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			       "Fatal Error: NULL namespace not allowed.");
	}
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->ns, tlen);
      tns    = buffer;
      qlen  += tlen;

      tlen   = strlen(name->name);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->name, tlen);
      tname  = buffer; 
      qlen  += tlen;

      tlen   = strlen(value);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, value, tlen);
      tval   = buffer;
      qlen  += tlen;
      
      qtempl = "SELECT assert('%s', '%s', '%s', '%s')";
      qlen  += strlen(qtempl);
      query = (char *) apr_palloc(db->pool, qlen);
      snprintf(query, qlen, qtempl, turi, tns, tname, tval);
      
      /* execute the database query and check return value */
      res   = PQexec(dbh->dbh, query);
      if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
	{
	  PQclear(res);
	  return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			       "Fatal Error: datbase error during  property storage.");	  
	}
      PQclear(res);

      return NULL;
}

dav_error *
dav_cms_db_remove(dav_db *db, const dav_prop_name *name)
{
  /* FIXME: how should we react if res.count <> 1? 
   * We also need to do better error checking.
   */

  if(dbh)
    {
      PGresult *res;
      char     *buffer;
      char     *qtempl, *query;
      char     *uri;

      char     *turi, *tns, *tname; 
      size_t    tlen;
      size_t    qlen;
      
      qlen   = 0;

      uri    = (char *) db->resource->uri;
      tlen   = strlen(uri);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, uri, tlen);
      turi   = buffer;      
      qlen  += tlen;

      tlen   = strlen(name->ns);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->ns, tlen);
      tns    = buffer;
      qlen  += tlen;

      tlen   = strlen(name->name);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->name, tlen);
      tname  = buffer; 
      qlen  += tlen;
      
      qtempl = "DELETE FROM facts WHERE uri = '%s' AND  namespace = '%s' AND  name = '%s'";
      qlen  += strlen(qtempl);
      query = (char *) apr_palloc(db->pool, qlen);
      snprintf(query, qlen, qtempl, turi, tns, tname);

      res   = PQexec(dbh->dbh, query);
      /*FIXME: errorchecking here */
      PQclear(res);
    }
  return NULL;
}

int
dav_cms_db_exists(dav_db *db, const dav_prop_name *name)
{
  PGresult *res;  
  char     *buffer, *qtempl, *query;
  char     *turi, *tns, *tname;
  size_t    tlen, qlen;
  int       exists;
    
  /* FIXME: is this a joke? No way to signal an error condition? */
  if(!dbh)
    {
      return 0;
    }
  
  qlen   = 0;
  
  tlen   = strlen(db->resource->uri);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, db->resource->uri, tlen);
  turi   = buffer;      
  qlen  += tlen;

  tlen   = strlen(name->ns);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, name->ns, tlen);
  tns    = buffer;
  qlen  += tlen;

  tlen   = strlen(name->name);
  buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
  tlen   = PQescapeString(buffer, name->name, tlen);
  tname  = buffer; 
  qlen  += tlen;

  qtempl = "SELECT * FROM facts "
    "WHERE uri = '%s' AND namespace ='%s' AND 'name = '%s'"; 
  qlen  += strlen(qtempl);
  query = (char *) apr_palloc(db->pool, qlen);
  snprintf(query, qlen, qtempl, turi, tns, tname);
      

  /* execute the database query and check return value */
  res   = PQexec(dbh->dbh, query);
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
      dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
		    "Fatal Error: datbase error during  property storage.");	  
    }
  exists = PQntuples(res);
  PQclear(res);
  return exists;
}

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
 * @tip 
 *   We are supposed to set both namespace and name to NULL
 *   to indicate that we are finished with all properties.
 */ 
dav_error *
dav_cms_db_first_name(dav_db *db, dav_prop_name *pname)
{
  pname->ns = pname->name = NULL;

  /* If we don't have a hash of properties, create one
   * and fill it with the properties.
   */
  if (db->cursor == NULL)
    {
      PGresult *res;
      char     *buffer, *qtempl, *query;
      char     *turi;
      size_t    tlen, qlen;

      
      if(!dbh)
	return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			"Fatal Error: not connected to database.");	  

      qlen   = 0;

      tlen   = strlen(db->resource->uri);
      buffer = (char *) apr_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, db->resource->uri, tlen);
      turi   = buffer;      
      qlen  += tlen;

      qtempl = "SELECT namespace, name, value FROM facts "
	       "WHERE uri = '%s'"; 
      qlen  += strlen(qtempl);
      query = (char *) apr_palloc(db->pool, qlen);
      snprintf(query, qlen, qtempl, turi);
      

      /* execute the database query and check return value */
      res   = PQexec(dbh->dbh, query);
      if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
	{
	  dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			"Fatal Error: datbase error during  property storage.");	  
	}

      /* SIDE NOTE: is this planed at all? Different properties
       * per URI.
       */
      /* move to the first entry */
      db->cursor = res;
      db->rows   = PQntuples(res); 
      db->pos    = 0;
    }

  
  /* now we start iterating over the entries in the property table.
   * If we are at the end of our result set we set pname->ns and
   * pname->name to NULL to indicate 'no more properties'.
   */
  if (db->pos == db->rows) 
    {
      pname->ns = pname->name = NULL;
      PQclear(db->cursor);
      db->cursor = NULL, db->pos = db->rows = 0;
    }
  else
    {
      /*FIXME: do we need to insert namespaces or their prefix here */   
      pname->ns   =  PQgetvalue(db->cursor, db->pos, 0);
      pname->name = (const char*) PQgetvalue(db->cursor, db->pos, 1);
      
      db->pos++;
    }
  return NULL;
}

dav_error *
dav_cms_db_next_name(dav_db *db, dav_prop_name *pname)
{
if (db->pos == db->rows) 
    {
      pname->ns  = pname->name = NULL;
      PQclear(db->cursor);
      db->cursor = NULL, db->pos = db->rows = 0;
    }
  else
    {
      /*FIXME: do we need to insert namespaces or their prefix here */   
      pname->ns   =  PQgetvalue(db->cursor, db->pos, 0);
      pname->name = (const char*) PQgetvalue(db->cursor, db->pos, 1);
      
      db->pos++;
    }
  return NULL;
}

dav_error *
dav_cms_db_get_rollback(dav_db *db, const dav_prop_name *name,
			dav_deadprop_rollback **prollback)
{
  return NULL;
}

dav_error *
dav_cms_db_apply_rollback(dav_db *db,
			 dav_deadprop_rollback *rollback)
{
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
