/**
 * @package dav_cms
 * @file    mod_dav_cms.c
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

#include <sys/limits.h>
#include <apr.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <stdio.h>
#include "mod_dav_cms.h"
#include "dav_cms_props.h"

/* FIXME: _no_ global/statics allowed! */
//FIXME: needs to be visible to dav_prpos.c //static 

const dav_provider *dav_backend_provider;
dav_provider dav_cms_provider;

/* forward-declare for use in configuration lookup */
module AP_MODULE_DECLARE_DATA dav_cms_module;


void
dav_cms_patch_provider(const char *newprov)
{
  char *prov;
  
  if (newprov)
    {
      prov = (char *) newprov;
    } 
  else 
    {
	prov = DAV_DEFAULT_BACKEND;
    }

  dav_backend_provider = NULL;
  dav_backend_provider = dav_lookup_provider(prov);
  if(dav_backend_provider)
    {
#ifndef NDEBUG
      fprintf(stderr, "[CMS]: ;-) Found backend DAV provider!\n");
#endif
      /* patch the provider table */
      dav_cms_provider.repos   = dav_backend_provider->repos;     /* storage          */
      dav_cms_provider.locks   = dav_backend_provider->locks;     /* resource locking */
      dav_cms_provider.vsn     = dav_backend_provider->vsn;       /* version control  */
      dav_cms_provider.binding = dav_backend_provider->binding;   /* ???              */
      /* insert our functionality */
      dav_cms_provider.propdb  = &dav_cms_hooks_propdb;
    }
}




/**
 **  Hooks that this module provides:
 **/

static int dav_cms_init(apr_pool_t *p, 
			apr_pool_t *plog, 
			apr_pool_t *ptemp,
			server_rec *s)
{
  ap_add_version_component(p, DAV_CMS_MODULE_NAME "/" DAV_CMS_VERSION);
  return OK;
}



/**
 * Callback functions for configuration commands.
 */

static void *dav_cms_create_server_conf(apr_pool_t *p, server_rec *s)
{
  dav_cms_server_conf *conf;

  conf = (dav_cms_server_conf *)apr_palloc(p, sizeof(dav_cms_server_conf));
  if(!conf)
    return NULL;
  conf->backend = (char *) NULL;
  conf->dsn     = (char *) NULL;
  conf->dbconn  = NULL;
  return conf;
}

static void *dav_cms_create_dir_conf(apr_pool_t *p, char *dir)
{
  dav_cms_dir_conf *conf; 

  conf = (dav_cms_dir_conf *)apr_palloc(p, sizeof(dav_cms_dir_conf));
  return conf;
}

static void *dav_cms_merge_server_conf(apr_pool_t *p,
				       void *base, 
				       void *overrides)
{
  return overrides;
}

static void *dav_cms_merge_dir_conf(apr_pool_t *p,
				    void *base, 
				    void *overrides)
{
  return NULL;
}

static const char *dav_cms_backend_cmd(cmd_parms  *cmd, 
				       void       *config,
				       const char *arg1)
{
  dav_cms_server_conf *conf;
  
  /* Find the server configuration */
  conf = (dav_cms_server_conf *)ap_get_module_config(cmd->server->module_config, &dav_cms_module);
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
      fprintf(stderr, "[CMS]: ;-) Found backend DAV provider!\n");
#endif
      return NULL;
      conf->backend = apr_pstrdup(cmd->pool, arg1);  
      /* patch the provider table */
      dav_cms_provider.repos   = dav_backend_provider->repos;     /* storage          */
      dav_cms_provider.locks   = dav_backend_provider->locks;     /* resource locking */
      dav_cms_provider.vsn     = dav_backend_provider->vsn;       /* version control  */
      dav_cms_provider.binding = dav_backend_provider->binding;   /* ???              */
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
  AP_INIT_TAKE1("CMS:Backend", dav_cms_backend_cmd, NULL, RSRC_CONF,
                "specify the name of the DAV backend module to use "),
  AP_INIT_TAKE1("CMS:DSN", dav_cms_dsn_cmd, NULL, RSRC_CONF,
                "specify DSN of the postgres database to use "),
  { NULL }
};

static void dav_cms_register_hooks(apr_pool_t *p)
{
  dav_cms_patch_provider((char *)NULL);
  /* Apache2 hooks we provide */
  ap_hook_post_config(dav_cms_init, NULL, NULL, APR_HOOK_MIDDLE);

  /* Apache2 mod_dav hooks we provide */

  dav_register_provider(p, DAV_CMS_PROVIDER, &dav_cms_provider);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA dav_cms_module = {
    STANDARD20_MODULE_STUFF, 
    dav_cms_create_dir_conf,      /* create per-dir    config structures */
    dav_cms_merge_dir_conf,       /* merge  per-dir    config structures */
    dav_cms_create_server_conf,   /* create per-server config structures */
    dav_cms_merge_server_conf,    /* merge  per-server config structures */
    dav_cms_cmds,                 /* table of config file commands       */
    dav_cms_register_hooks        /* register hooks                      */
};


/* 
 * local variables:
 * eval: ()
 * end:
 */
