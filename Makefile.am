SUBDIRS = src
TARGET  = rufus
TAGVER  = $(shell git log --oneline | wc -l)
SEDCMD  = s/^\([ \t]*\)Version="\([0-9]*\)\.\([0-9]*\)\.[0-9]*\.\([0-9]*\)"\(.*\)/\1Version="\2.\3.@@TAGVER@@.\4"\5/

# This step produces the UPX compressed and signed releases that are made available for public download
# NB: UPX v3.09 or later is needed for LZMA compression (http://upx.sourceforge.net/)
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@sleep 1
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx --lzma --best $(TARGET)$(EXEEXT)
	@mv $(TARGET)$(EXEEXT) $(TARGET)-$(VERSION)$(SUFFIX)$(EXEEXT)
	@cmd.exe //c _sign.cmd $(TARGET)-$(VERSION)$(SUFFIX)$(EXEEXT)

appx: $(TARGET)-$(VERSION)$(SUFFIX)$(EXEEXT)
	$(file > cmd.sed,$(SEDCMD))
	@sed -i -e "s/@@TAGVER@@/$(TAGVER)/g" cmd.sed
	@sed -b -i -f cmd.sed res/appstore/AppxManifest.xml
	@rm cmd.sed
	@cd res/appstore; cmd.exe //c packme.cmd
