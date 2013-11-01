all-local: embedded.loc

BUILT_SOURCES = embedded.loc
noinst_PROGRAMS =
noinst_EXES =

pkg_v_sed = $(pkg_v_sed_$(V))
pkg_v_sed_ = $(pkg_v_sed_$(AM_DEFAULT_VERBOSITY))
pkg_v_sed_0 = @echo "  SED    $<";

embedded.loc: rufus.loc
	$(pkg_v_sed)$(SED) -f embedded.sed $< > $@

clean-local:
	-rm -rf embedded.loc
