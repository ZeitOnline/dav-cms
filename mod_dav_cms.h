
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

  
/**
 ** Configuration Structures
 **/

/**
 * dav_cms_server_conf
 * This struct holds per server configuration
 * options for the dav_cms module.
 */
  typedef struct {
    int   magic;
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



/*!
 * returns NULL on successfull parameter execution or
 * a character string (char *) with the error message
 * on failure.
 * \param cmd 
 * \param config A pointer to the dav_cms_server_conf struct.
 *               This needs to be typecast from (void *).
 * \param arg1   The actual parameter as a char*.
 */
static const char *dav_cms_backend_cmd(cmd_parms  *cmd, 
				       void       *config,
				       const char *arg1);  


#ifdef __cplusplus
}
#endif

#endif /* _MOD_DAV_SVN_H_ */

