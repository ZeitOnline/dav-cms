
#ifndef _DAV_CMS__PROPS_H_
#define _DAV_CMS_PROPS_H_

/**
 * @package dav_cms
 */

#ifdef __cplusplus
extern "C" {
#endif


  /**
   * Prototypes for the dav_cms property database hooks */
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

