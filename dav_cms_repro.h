#ifndef _DAV_CMS_REPRO_H_
#define _DAV_CMS_REPRO_H_

/**
 * @package dav_cms
 */

#ifdef __cplusplus
extern "C" {
#endif
    dav_error * dav_cms_get_resource(request_rec *r, 
                                     const char *root_dir,
                                     const char *label, 
                                     int use_checked_in, 
                                     dav_resource **resource);

    dav_error *
    dav_cms_move_resource_i(dav_resource *src,
                                        dav_resource *dst,
                                        dav_response **response);


    dav_error * 
    dav_cms_copy_resource_i(
        const dav_resource *src,
        dav_resource *dst,
        int depth,
        dav_response **response
    );

#ifdef __cplusplus
}
#endif

#endif /* _DAV_CMS_REPRO_H_ */

