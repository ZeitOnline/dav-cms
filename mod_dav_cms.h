
#ifndef _MOD_DAV_CMS_H_
#define _MOD_DAV_CMS_H_

/**
 * @package dav_cms
 */

#ifdef __cplusplus
extern "C" {
#endif

#define DAV_CMS_PROVIDER     "zeit-cms"
#define DAV_CMS_MODULE_NAME  "ZeitCMS"
#define DAV_CMS_VERSION      "0.1a"

extern dav_provider dav_cms_provider;  

 /**
 ** Configuration Handling
 **/

/**
 * dav_cms_server_conf
 * This struct holds per server configuration
 * options for the dav_cms module.
 */
  typedef struct {
    char *backend;
    char *dsn;
    void *dbconn;
    /** FIXME: do we need a transaction lock ? **/
  } dav_cms_server_conf;
  
  /**
   * dav_cms_server_conf
   * This struct holds per directory configuration
   * options for the dav_cms module.
   */
  typedef struct {
    int magic;
    ; /* void for now */
  } dav_cms_dir_conf;
  

#ifdef __cplusplus
}
#endif

#endif /* _MOD_DAV_SVN_H_ */

