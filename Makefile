# APXS = @apxs@
APXS = /usr/bin/apxs
APR_CONFIG = apr-config
PG_CONFIG  = pg_config

SRC      = mod_dav_cms.c dav_cms_monitor.c dav_cms_props.c

HEADERS  = dav_cms_monitor.h dav_cms_props.h mod_dav_cms.h

INCLUDES = `$(APR_CONFIG) --includedir` \
           -I`$(PG_CONFIG) --includedir`

LDFLAGS  = `$(APR_CONFIG) --link-ld --libs` \
           -L `$(PG_CONFIG) --libdir` -lpq

DESTDIR  = `$(APXS) -q LIBEXECDIR`
 
all: $(SRC) $(HEADERS)
	$(APXS) -I$(INCLUDES) $(LDFLAGS) -c $(SRC)

install: .libs/mod_dav_cms.so
	install -m 600 -o www-data .libs/mod_dav_cms.so $(DESTDIR) 
