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

dav_error *
dav_cms_db_open(apr_pool_t *p, const dav_resource *resource, int ro, dav_db **pdb)
{
  dav_db           *db;

  if(!dbh)
    dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Fatal Error: no database connection set up");

  /* really open database connection */
  if(dav_cms_db_connect(dbh) != CMS_OK)
    dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Error connection to property database");


  db = (dav_db *)apr_pcalloc(p, sizeof(*db));
  db->resource = resource;
  db->pool     = p;
  *pdb = db;

  /* FIXME: in reality we would either open a connection to the
   * postgres backend or begin a transaction on an open connection.
   */
  if (dav_cms_start_transaction() != CMS_OK)
    dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, 0,
		  "Error establishing transaction in property database");


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
   dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
		 "Error commiting transaction in property database\n"
		 "Your operation might not be stored in the backend");
 /* FIXME: Just to keep compiler happy*/
 if(0) dav_cms_rollback();
}

dav_error *
dav_cms_db_define_namespaces(dav_db *db, dav_xmlns_info *xi)
{
  /* FIXME: to my understanding, we are asked to insert our namespaces
   * (i.e. the ones we manage for the given resource) into the xml
   * namespace info struct. In realiter we want to fetch all
   * namespaces known to us from the database.
   */
  dav_xmlns_add(xi, DAV_CMS_NS_PREFIX, DAV_CMS_NS);
  return NULL;
}

dav_error *
dav_cms_db_output_value(dav_db *db, const dav_prop_name *name,
			dav_xmlns_info  *xi,
			apr_text_header *phdr, 
			int *found)
{
  const char *prefix = NULL;   /* Namespace prefix    */
  char       *buffer = NULL;   /* String to be output */

  /* FIXME: how can we access the server configuration from here ? */
  if(dav_cms_db_connect(NULL) != CMS_OK)
    {
      dav_cms_db_disconnect(NULL);
      return dav_new_error(db->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			   "Unable to connect to database");
    }
#ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
	       "[cms]: Looking for `%s' : `%s'\n", name->ns, name->name);
#endif
  
  *found = 0;
 
  if (name->ns)
    {
      /* find the prefix for this URI */
      prefix = dav_xmlns_get_prefix(xi, name->ns);
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
		   "[cms]: Mapping `%s' -> `%s'\n", name->ns, prefix);
    }
  if(prefix) 
    {
      buffer = apr_psprintf(db->pool,"<%s:%s>%s</%s:%s>",
			    prefix,
			    name->name,
			    "Glorp",
			    prefix,
			    name->name);
      apr_text_append(db->pool, phdr, buffer);
    }
  else
    {
      buffer = apr_psprintf(db->pool,"<%s>%s</%s>",
			    name->name,
			    "Glorp",
			    name->name);
      apr_text_append(db->pool, phdr, buffer);
    }
  *found = 1;
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

  /* FIXME: the following algorythm needs to be implemented:
   * 
   * -# compare the namespace with the hash of namespaces we
   *    are interested in.
   * -# If it is found, store the property in the database,
   *    possibly overriding an older value (stored proc.).
   * -# If not, dispatch to the backend providers store function.
   */
  char        *value   = NULL;
  apr_size_t   valsize = 0;

  apr_xml_to_text(db->pool, elem, APR_XML_X2T_INNER, NULL, 0,
		  (const char **) &value, &valsize);
  if(value)
    value[valsize] = (char) 0;
#ifndef NDEBUG
  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, 
	       "[cms]: Storing '%s : %s'\n\t%s", name->ns, name->name, value);
#endif

  /* FIXME: store the value in the database */
  // return (*dav_backend_provider->propdb->store)(db, name, elem, mapping);

  if(dbh)
    {
      PGresult *res;
      char     *buffer;
      char     *qtempl, *query;
      char     *uri;
      char     *turi, *tns, *tname, *tval; 
      size_t    tlen;
      size_t    qlen;
      
      qlen   = 0;

      uri    = (char *) db->resource->uri;
      tlen   = strlen(uri);
      buffer = (char *) ap_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, uri, tlen);
      turi   = buffer;      
      qlen  += tlen;

      tlen   = strlen(name->ns);
      buffer = (char *) ap_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->ns, tlen);
      tns    = buffer;
      qlen  += tlen;

      tlen   = strlen(name->name);
      buffer = (char *) ap_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, name->name, tlen);
      tname  = buffer; 
      qlen  += tlen;

      tlen   = strlen(value);
      buffer = (char *) ap_palloc(db->pool, 2 *(tlen + 1));
      tlen   = PQescapeString(buffer, value, tlen);
      tval   = buffer;
      qlen  += tlen;
      
      qtempl = "INSERT INTO attributes (uri, namespace, name, value) VALUES ('%s', '%s', '%s', '%s')";
      qlen  += strlen(qtempl);
      query = (char *) ap_palloc(db->pool, qlen);
      snprintf(query, qlen, qtempl, turi, tns, tname, tval);
      res   = PQexec(dbh->dbh, query);
    }
  return NULL;
}

dav_error *
dav_cms_db_remove(dav_db *db, const dav_prop_name *name)
{
  return NULL;
}

int
dav_cms_db_exists(dav_db *db, const dav_prop_name *name)
{
  return 1;
}

/**
 * @function dav_cms_db_first_name
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
  if (db->props == NULL)
    {
      db->props = apr_hash_make(db->pool);
      /* FIXME: sample data only! In realiter we would send
       * an SQL query to the backend to get all ns/properties
       * for this URI.
       * SIDE NOTE: is this planed at all? Different properties
       * per URI.
       */
      apr_hash_set(db->props, "test", APR_HASH_KEY_STRING, "PropA");
      apr_hash_set(db->props, "test2", APR_HASH_KEY_STRING, "PropB");
    }
  /* now we start iterating over the entries in the property table */
  db->hi = apr_hash_first(db->pool, db->props);
  if(db->hi != NULL)
    {
      const void *name = NULL;
      
      apr_hash_this(db->hi, &name, NULL, NULL);
      pname->ns = DAV_CMS_NS; 
      pname->name = (const char*) name;
    }
  return NULL;
}

dav_error *
dav_cms_db_next_name(dav_db *db, dav_prop_name *pname)
{
  /* init pname */
  pname->ns = pname->name = NULL;

  /* increment pointer to hash entry */
  db->hi = apr_hash_next(db->hi); 
  if (db->hi != NULL)
    {
      const void *name = NULL;
  
      pname->ns = DAV_CMS_NS;
      apr_hash_this(db->hi, &name, NULL, NULL);
      pname->name = (const char *) name;
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
