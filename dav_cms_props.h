
#ifndef _DAV_CMS_PROPS_H_
#define _DAV_CMS_PROPS_H_

/**
 * @package dav_cms
 */

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * This is our own version of struct dav_db. This struct
   * is typed in mod_dav.h but only as an alias to a not
   * public structure in mod_dav.c -- hence we need to redefine
   * it here.
   * FIXME: most of the code here is taken rather literal from
   * mod_Dav_svn.[ch].
   */

struct dav_namespace_map {
    int                *ns_map;
    apr_array_header_t *namespaces;
};

struct dav_db {
  const dav_resource *resource;
  char               *uri;
  apr_pool_t         *pool;
  apr_hash_t         *props;   /* the resource's properties that */
  apr_hash_index_t   *hi;      /* we are sequencing over         */

  /* Ok, this _is_ just testcode. Can we pass a resultset arround?
   * My fear is that transactions/cursors stay open.
   */
  PGconn             *conn;
  PGresult           *cursor;
  
  int                 rows;
  int                 pos;
  /* Support for lazy transaction handling
   * should_transact is set whenever mod_dav thinks
   * a transaction needs to be started. The in_transact
   * is set whenever mod_dav_cms actually opens a transaction.
   */
  int                 DTL : 1;
  int                 PTL : 1;
};


extern const dav_hooks_propdb dav_cms_hooks_propdb;
extern const dav_hooks_search dav_cms_hooks_search;
  
  /*
   * Prototypes for the dav_cms property database hooks 
   */

dav_error *
dav_cms_db_open(apr_pool_t *p, const dav_resource *resource, int ro, dav_db **pdb);
    
void
dav_cms_db_close(dav_db *db);

dav_error *
dav_cms_db_define_namespaces(dav_db *db, dav_xmlns_info *xi);

dav_error *
dav_cms_db_output_value(dav_db *db, const dav_prop_name *name,
		     dav_xmlns_info *xi,
		     apr_text_header *phdr, int *found);

dav_error *
dav_cms_db_map_namespaces(dav_db *db,
		       const apr_array_header_t *namespaces,
		       dav_namespace_map **mapping);

dav_error * 
dav_cms_db_store(dav_db *db, const dav_prop_name *name,
	      const apr_xml_elem *elem,
	      dav_namespace_map *mapping);

dav_error *
dav_cms_db_remove(dav_db *db, const dav_prop_name *name);

int
dav_cms_db_exists(dav_db *db, const dav_prop_name *name);

dav_error *
dav_cms_db_first_name(dav_db *db, dav_prop_name *pname);

dav_error *
dav_cms_db_next_name(dav_db *db, dav_prop_name *pname);

dav_error *
dav_cms_db_get_rollback(dav_db *db, const dav_prop_name *name,
			dav_deadprop_rollback **prollback);

dav_error *
dav_cms_db_apply_rollback(dav_db *db,
			 dav_deadprop_rollback *rollback);



#ifdef __cplusplus
}
#endif

#endif /* _DAV_CMS_PROPS_H_ */

