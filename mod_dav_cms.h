#ifndef _MOD_DAV_CMS_H_
#define _MOD_DAV_CMS_H_

/**
  * @package dav_cms
  * @mainpage mod_dav_cms
  *
  * @section Intro
  *
  * @section Server Configuration
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __GNUC__
#define __inline__
#endif

#define DAV_CMS_PROVIDER     "zeit-cms"
#define DAV_DEFAULT_BACKEND  "filesystem"
#define DAV_CMS_MODULE_NAME  "ZeitCMS"
#define DAV_CMS_VERSION      "0.1a"


#define DAV_CMS_NS           "http://namespaces.zeit.de/CMS"
#define DAV_CMS_NS_PREFIX    "CMS"

#define OFF 0
#define ON  1



  /** 
   * Module specific error codes 
   */
  typedef enum {CMS_OK=0, CMS_FAIL} dav_cms_status_t;


  /**
   * Our database connection
   *
   *
   */
  typedef struct {
    char   *dsn;                /**< Database connection string.  */
    PGconn *dbh;                /**< Open database connection handle or NULL. */
    /** FIXME: do we need a transaction lock ? **/
  } dav_cms_dbh;



  /**
   * Configuration Handling
   */

  
  /**
   * @struct dav_cms_server_conf
   * @brief  This struct holds per server configuration
   *         options for the dav_cms module.
   */
  typedef struct {
    char        *backend;              /**< The name of the backend provider.           */
    char        *dsn;                  /**< The database connection settings for this server */
  } dav_cms_server_conf;
  
  /**
   * @struct dav_cms_dir_conf
   * @brief  This struct holds per directory configuration
   *         options for the dav_cms module.
   */
  typedef struct {
    int magic;
    ; /* void for now */
  } dav_cms_dir_conf;
  


  /**
   * Function declarations
   */

  /**
   * @function dav_cms_patch_provider
   *
   * This function will install mod_dav_cms into mod_davs provider
   * table. The following steps are performed:
   * 
   * -# it attempts to get a pointer to the backend
   *    provider that is responsible to handle all
   *    functions that we don't handle.
   *
   * -# if found, it will grab references to these
   *    functions and install them in our own provider
   *    hook table.
   * 
   * -# finally the own functions are inserted into the
   *    hook table.
   *
   * @param newprov the name of the backend provider.
   */
  void         dav_cms_patch_provider(const char *newprov);



  /**
   * Module global data structures
   */
  extern        dav_provider  dav_cms_provider;  
  extern  const dav_provider *dav_backend_provider;
  extern        dav_cms_dbh  *dbh;


#ifdef __cplusplus
}
#endif

#endif /* _MOD_DAV_SVN_H_ */

