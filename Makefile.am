SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed releases that are made available for public download
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@mv src/$(TARGET)_fd$(EXEEXT) .
	@$(STRIP) $(TARGET)$(EXEEXT)
	@$(STRIP) $(TARGET)_fd$(EXEEXT)
	@upx $(TARGET)$(EXEEXT)
	@upx $(TARGET)_fd$(EXEEXT)
	@cmd.exe /k _sign.cmd $(TARGET)$(EXEEXT) $(TARGET)_fd$(EXEEXT)
