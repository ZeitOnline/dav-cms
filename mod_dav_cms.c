/**
 * Filespec: $Id$
 *
 * Filename:      mod_dav_cms.c
 * Author:        <rm@fabula.de> 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
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
#include <http_log.h>
#include <http_config.h>
#include <http_protocol.h>
#include <apr.h>
#include <apr_pools.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <postgresql/libpq-fe.h>
#include "mod_dav_cms.h"
#include "dav_cms_props.h"
#include "dav_cms_monitor.h"

static volatile char ident_string[] = "$Id$";

/* FIXME: _no_ global/statics allowed! */
/* FIXME: This _will_ break terribly if used
 * in threaded code!
 */

dav_cms_dbh *dbh;

/* FIXME: needs to be visible to 'dav_cms_props.c'*/
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
         ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "[CMS]: Found backend DAV provider!");
      #endif
      /* patch the provider table */
         dav_cms_provider.repos   = dav_backend_provider->repos;     /* storage          */
         dav_cms_provider.locks   = dav_backend_provider->locks;     /* resource locking */
         dav_cms_provider.vsn     = dav_backend_provider->vsn;       /* version control  */
         dav_cms_provider.binding = dav_backend_provider->binding;   /* ???              */
      /* insert our functionality */
         dav_cms_provider.propdb  = &dav_cms_hooks_propdb;
         dav_cms_provider.search  = &dav_cms_hooks_search;         
      }
      else 
      {
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "[CMS]: Did not find a sufficient backend DAV provider ('%s')!", DAV_DEFAULT_BACKEND);
      exit (0);
      }
   }



/**
 * @function dav_cms_child_destroy
 *
 * This function is registered as a cleanup function during module
 * initialisation.  Called during the release of the child resource
 * pool it will release all database resources still occupied by the
 * child process.
 *
 * @param ctxt The dbconnection that might hold a still
 * open database connection.
 */
   static apr_status_t
   dav_cms_child_destroy(void *ctxt)
   {
#     ifndef NDEBUG
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL,  "[cms]: Cleaning up resources");
#     endif

      if(dbh)
      {
         if(dbh->dbh)
            PQfinish(dbh->dbh);
         dbh->dbh = NULL;
         dbh = NULL;
#        ifndef NDEBUG
	 ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL,  "[cms]: Closed database connection");
#        endif
      }
      return APR_SUCCESS;
   }



/**
 * Hooks that this module provides:
 */

/**
 * @param pchild     server child process resource pool
 * @param plog       logging resource pool
 * @param ptemp      temporary resource pool
 * @param server_rec server record struct
 * 
 * Besides leaving our marks in the servers version string we try to
 * allocate memory to hold our database settings and a connection
 * handle.
 *
 * We register a cleanup function to ensure propper releasing of all
 * database related resources.
 */
   static int dav_cms_init(apr_pool_t *pchild,
   apr_pool_t *plog, 
   apr_pool_t *ptemp,
   server_rec *server)
   {
      dav_cms_server_conf *conf;
   
      ap_add_version_component(pchild, DAV_CMS_MODULE_NAME "/" DAV_CMS_VERSION);
   
   /* FIXME: how do i get my hands on 'conf' */
      conf = (dav_cms_server_conf *)ap_get_module_config(server->module_config, &dav_cms_module);
      if(!conf)
	printf("No configuration data found");
   
   /* Allocate our database connection struct from the child pool ... */
   
      if(!dbh)
	dbh = (dav_cms_dbh *)apr_palloc(pchild, sizeof(dav_cms_dbh));
   
      if(dbh)
      {
         printf("DSN is: %s", conf->dsn); 
         dbh->dsn = apr_pstrdup(pchild, conf->dsn);
         dbh->dbh = NULL;
      } 
      else 
      {
         exit(255);
      }
   
      /* ... and register a cleanup */
      apr_pool_cleanup_register(pchild, NULL, 
				dav_cms_child_destroy, 
				dav_cms_child_destroy);
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
      conf->backend  = (char *) NULL;
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
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
         "[CMS]: Request to use '%s' as a backend DAV module.", arg1);
   #endif
      dav_backend_provider = NULL;
      dav_backend_provider = dav_lookup_provider(arg1);
      if(dav_backend_provider)
      {
      #ifndef NDEBUG
         ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
            "[CMS]: Found backend DAV provider!");
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
         ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, 
            "[CMS]: Couldn't get backend DAV provider");
         return "\tCMSbackend: no DAV provider with that name.";
      }
   }

   static const char *dav_cms_dsn_cmd(cmd_parms  *cmd,
   void       *config,
   const char *arg1)
   {
   
      dav_cms_server_conf *conf;
   
      conf = (dav_cms_server_conf *)ap_get_module_config(cmd->server->module_config, &dav_cms_module);
      conf->dsn = apr_pstrdup(cmd->pool, arg1);
   #ifndef NDEBUG
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, 
         "[CMS]: Request to use %s as a database server", conf->dsn);
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
   AP_INIT_TAKE1("CMS:DSN",     dav_cms_dsn_cmd,     NULL, RSRC_CONF,
   "specify DSN of the (postgresql) database to use "),
   { NULL }
   };

   static void dav_cms_register_hooks(apr_pool_t *p)
   {
      dav_cms_patch_provider((char *)NULL);
   /* Apache2 hooks we provide */
      ap_hook_post_config(dav_cms_init, NULL, NULL, APR_HOOK_MIDDLE);
      ap_hook_log_transaction(dav_cms_monitor ,NULL, NULL, APR_HOOK_MIDDLE);
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
