mod_dav_cms.la: mod_dav_cms.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_dav_cms.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_dav_cms.la
