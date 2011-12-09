SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed release that is made available for public download
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx $(TARGET)$(EXEEXT)
	@cmd.exe /k _sign.cmd $(TARGET)$(EXEEXT)
