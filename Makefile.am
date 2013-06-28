SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed releases that are made available for public download
# NB: UPX v3.09 or later is needed for LZMA compression (http://upx.sourceforge.net/)
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx --lzma $(TARGET)$(EXEEXT)
	@mv $(TARGET)$(EXEEXT) $(TARGET)_v$(VERSION)$(EXEEXT)
	@cmd.exe /k _sign.cmd $(TARGET)_v$(VERSION)$(EXEEXT)
