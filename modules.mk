mod_dav_cms.la: mod_dav_cms.slo dav_cms_props.slo dav_cms_monitor.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_dav_cms.lo dav_cms_props.lo dav_cms_monitor.lo -lpq
DISTCLEAN_TARGETS = modules.mk
shared =  mod_dav_cms.la
