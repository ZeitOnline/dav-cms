/**
 * @package dav_cms
 */
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <stdio.h>
#include "mod_dav_cms.h"
#include "dav_cms_props.h"


dav_error *
dav_cms_db_open(apr_pool_t *p, const dav_resource *resource, int ro, dav_db **pdb)
{
  dav_db *db;

  db = (dav_db *)apr_pcalloc(p, sizeof(*db));

  db->resource = resource;
  db->p = p;
  *pdb = NULL;

#ifndef NDEBUG
  fprintf(stderr, "[cms]: Opening database '%s'\n", resource->uri);
#endif
  return NULL;
}
    
void
dav_cms_db_close(dav_db *db)
{

}

dav_error *
dav_cms_db_define_namespaces(dav_db *db, dav_xmlns_info *xi)
{
  return NULL;
}

dav_error *
dav_cms_db_output_value(dav_db *db, const dav_prop_name *name,
			dav_xmlns_info *xi,
			apr_text_header *phdr, int *found)
{
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

dav_error *
dav_cms_db_first_name(dav_db *db, dav_prop_name *pname)
{
  return NULL;
}

dav_error *
dav_cms_db_next_name(dav_db *db, dav_prop_name *pname)
{
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
