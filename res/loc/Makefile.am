all-local: embedded.loc

BUILT_SOURCES = embedded.loc
noinst_PROGRAMS =
noinst_EXES =

AM_V_SED_0 = @echo "  SED    $<";$(SED)
AM_V_SED_1 = $(SED)
AM_V_SED_  = $(AM_V_SED_$(AM_DEFAULT_VERBOSITY))
AM_V_SED   = $(AM_V_SED_$(V))

embedded.loc: rufus.loc
	$(AM_V_SED) -f embedded.sed $< > $@

clean-local:
	-rm -rf embedded.loc
