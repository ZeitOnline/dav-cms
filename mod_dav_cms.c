/**
 * @package dav_cms
 */

/* 
**  mod_dav_cms.c -- Apache DAV provider module
**
** =================================================================
**    CONFIGURATION OPTIONS
** =================================================================
**
**  In the apache configuration file: 
**   
**  CMSBackend  dav backend module.
**
**  CMSdsn      database source for the cms module
**
**/ 

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <stdio.h>
#include "mod_dav_cms.h"

/* FIXME: _no_ global/statics allowed! */
const static dav_provider *dav_backend_provider;




/**
 **  Hooks that this module provides:
 **/

static int dav_cms_init(apr_pool_t *p, 
			apr_pool_t *plog, 
			apr_pool_t *ptemp,
			server_rec *s)
{
  ap_add_version_component(p, DAV_CMS_MODULE_NAME DAV_CMS_VERSION);
  return OK;
}



/**
 * Callback functions for configuration commands.
 */

static const char *dav_cms_create_server_conf()
{
  
}

static const char *dav_cms_create_dir_conf()
{

}

static const char *dav_cms_merge_server_conf()
{
  
}

static const char *dav_cms_merge_dir_conf()
{
  
}

static const char *dav_cms_backend_cmd(cmd_parms  *cmd, 
				       void       *config,
				       const char *arg1)
{
  //dav_svn_dir_conf *conf = config;
  //conf->fs_path = apr_pstrdup(cmd->pool, arg1);
  /*
   * Try to fetch the backend DAV provider.
   * FIXME: this shouldn't be a module static but rather be part
   * of the dav_cms_conf data.
   */
#ifndef NDEBUG
  fprintf(stderr, "[CMS]: Request to use '%s' as a backend DAV module.\n", arg1);
#endif
  dav_backend_provider = NULL;
  dav_backend_provider = dav_lookup_provider(arg1);
  if(dav_backend_provider)
    {
#ifndef NDEBUG
      fprintf(stderr, "[CMS]: Found backend DAV provider!\n");
#endif
      /* patch the provider table */
      return NULL;
    }
  else 
    {
#ifndef NDEBUG 
      fprintf(stderr, "[CMS]: Couldn't get backend DAV provider\n");
#endif
      return "\tCMSbackend: no DAV provider with that name.";
    }
}

static const char *dav_cms_dsn_cmd(cmd_parms  *cmd,
				   void       *config,
				   const char *arg1)
{
  //dav_dir_conf *conf = config;

  //conf->fs_path = apr_pstrdup(cmd->pool, arg1);
#ifndef NDEBUG
  fprintf(stderr, "[CMS]: Request to use %s as a database server\n", arg1);
#endif
  return NULL;
}




/** 
 * The following is the usual Apache2 DSO module stuff.
 **/

static const command_rec dav_cms_cmds[] =
{
  /* per directory/location */
  
  /* per server */
  AP_INIT_TAKE1("CMSbackend", dav_cms_backend_cmd, NULL, RSRC_CONF,
                "specify the name of the DAV backend module to use "),
  AP_INIT_TAKE1("CMSdsn", dav_cms_dsn_cmd, NULL, RSRC_CONF,
                "specify DSN of the postgres database to use "),
  { NULL }
};

static const dav_provider dav_cms_provider =
{
  NULL,                       /* storage */
  dav_cms_hooks_propdb,       /* property db */
  NULL,                       /* locks */
  NULL,                       /* versioning */ 
  NULL                        /* binding */
};


static void dav_cms_register_hooks(apr_pool_t *p)
{
  /* Apache2 hooks we provide */
  ap_hook_post_config(dav_cms_init, NULL, NULL, APR_HOOK_MIDDLE);

  /* Apache2 mod_dav hooks we provide */
  dav_register_provider(p, DAV_CMS_PROVIDER, &dav_cms_provider);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA dav_cms_module = {
    STANDARD20_MODULE_STUFF, 
    NULL,                   /* create per-dir    config structures */
    NULL,                   /* merge  per-dir    config structures */
    NULL,                   /* create per-server config structures */
    NULL,                   /* merge  per-server config structures */
    dav_cms_cmds,           /* table of config file commands       */
    dav_cms_register_hooks  /* register hooks                      */
};


/* 
 * local variables:
 * eval: ()
 * end:
 */
