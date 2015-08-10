SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed releases that are made available for public download
# NB: UPX v3.09 or later is needed for LZMA compression (http://upx.sourceforge.net/)
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@sleep 1
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx --lzma $(TARGET)$(EXEEXT)
	@mv $(TARGET)$(EXEEXT) $(TARGET)-$(VERSION)$(SUFFIX)$(EXEEXT)
	@cmd.exe //c _sign.cmd $(TARGET)-$(VERSION)$(SUFFIX)$(EXEEXT)
