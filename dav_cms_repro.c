#include <httpd.h>
#include <http_log.h>
#include <mod_dav.h>
#include "mod_dav_cms.h"

extern        dav_provider  dav_cms_provider;  
extern  const dav_provider *dav_backend_provider;

extern const struct dav_hooks_repository *orig_repos_vt;
extern struct dav_hooks_repository       *dav_cms_repos_vt;

//extern        dav_cms_dbh  *dbh;

dav_error*
 dav_cms_get_resource(request_rec *r, const char *root_dir,
                     const char *label, int use_checked_in, dav_resource **resource) 
{
    dav_error *result;
    
    fprintf(stderr, "[TRACE] %s on %s\n", r->method, r->uri); 
    /* call backend code */
    if (M_PUT == r->method_number) {
        fprintf(stderr, "PUTTING %s\n", r->uri);
    }
    
    result = orig_repos_vt->get_resource(r, root_dir, label, use_checked_in, resource);
    if ((*resource)->hooks == orig_repos_vt) {
        fprintf(stderr, "Wrong repos hoosks for resource\n");
    }
    return result;
}

dav_error * 
    dav_cms_copy_resource_i(const dav_resource *src, dav_resource *dst,
        int depth, dav_response **response)
{
    dav_error *result;
    
    DBG2("Copying %s to %s\n", src->uri, dst->uri);
    result = orig_repos_vt->copy_resource(src, dst, depth, response);
    return result;

}


dav_error* 
dav_cms_move_resource_i(dav_resource *src, dav_resource *dst, dav_response **response)
{
    dav_error *result;
    
    DBG2("Moving %s to %s\n", src->uri, dst->uri);
    result = orig_repos_vt->move_resource(src, dst, response);
    return result;
}

