# Create delay-loaded libraries from a DLL, that aren't vulnerable to side-loading
AM_V_DLLTOOL_0 = @echo "  LIB      $@";$(DLLTOOL)
AM_V_DLLTOOL_1 = $(DLLTOOL)
AM_V_DLLTOOL_  = $(AM_V_DLLTOOL_$(AM_DEFAULT_VERBOSITY))
AM_V_DLLTOOL   = $(AM_V_DLLTOOL_$(V))

AM_V_SED_0     = @echo "  SED      $<";$(SED)
AM_V_SED_1     = $(SED)
AM_V_SED_      = $(AM_V_SED_$(AM_DEFAULT_VERBOSITY))
AM_V_SED       = $(AM_V_SED_$(V))

# Ah the joys of Windows DLL calling conventions, that require an @## suffix in the .def
# for x86_32 and but no @## for x86_64, thereby forcing us to strip stuff according to the
# target arch. Oh, and we can't use 'target_cpu' or AC definitions on account that we are
# switching archs when building on our local machine, and don't want to have to go though
# a costly reconf each time when we can simply issue a 'make clean'.
TUPLE         := $(shell $(CC) -dumpmachine)
TARGET        := $(word 1,$(subst -, ,$(TUPLE)))
DEF_SUFFIX    := $(if $(TARGET:x86_64=),.def,.def64)

.PHONY: all
all: dwmapi-delaylib.lib version-delaylib.lib wintrust-delaylib.lib

%.def64: %.def
	$(AM_V_SED) "s/@.*//" $< >$@

%-delaylib.lib: %$(DEF_SUFFIX)
	$(AM_V_DLLTOOL) --input-def $< --output-delaylib $@ --dllname $(basename $<).dll

clean:
	$(RM) -rf *.lib
