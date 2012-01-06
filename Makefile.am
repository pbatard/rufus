SUBDIRS = src
TARGET  = rufus

# This step produces the UPX compressed and signed releases that are made available for public download
release: all
	@mv src/$(TARGET)$(EXEEXT) .
	@$(STRIP) $(TARGET)$(EXEEXT)
	@upx $(TARGET)$(EXEEXT)
	@mv $(TARGET)$(EXEEXT) $(TARGET)_v$(VERSION)$(EXESUFFIX)$(EXEEXT)
# Don't want to enter a password twice => only sign when we have both release files
	@if [ -f $(TARGET)_v$(VERSION)f$(EXEEXT) ]; then \
		if [ -f $(TARGET)_v$(VERSION)$(EXEEXT) ]; then \
			cmd.exe /k _sign.cmd $(TARGET)_v$(VERSION)$(EXEEXT) $(TARGET)_v$(VERSION)f$(EXEEXT); \
		fi; \
	fi