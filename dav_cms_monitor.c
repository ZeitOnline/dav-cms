/**
 * @package dav_cms
 */

#include <httpd.h>
#include <http_log.h>
#include <http_config.h>
#include <http_protocol.h>
#include <ap_config.h>
#include <apr_strings.h>
#include <mod_dav.h>
#include <postgresql/libpq-fe.h>
#include "mod_dav_cms.h"
#include "dav_cms_monitor.h"

/* Dummy implemetation for now */

int dav_cms_monitor(request_rec *r)
{
  char *src;
  char *dest;
  
  src  = r->uri;
  dest = "";

  switch(r->method_number)
    {
    case M_COPY:
    case M_MOVE:
      dest =  (char *) apr_table_get(r->headers_in, "Destination");
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "(TRIGGER '%s \"%s\" \"%s\")\n", 
		   r->method, src, dest);
      break;
    case M_PROPPATCH:
    case M_DELETE:
    case M_PUT:
    default:
      /*etc., etc. ad nauseam ... */
       ap_log_error(APLOG_MARK, APLOG_WARNING, 0, NULL, "(TRIGGER '%s \"%s\")\n", 
		    r->method, src);
      break;
    }
  return OK;
}
