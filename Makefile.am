SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed releases that are made available for public download
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx $(TARGET)$(EXEEXT)
	@mv $(TARGET)$(EXEEXT) $(TARGET)_v$(VERSION)$(EXEEXT)
	@cmd.exe /k _sign.cmd $(TARGET)_v$(VERSION)$(EXEEXT)
