# This sed script removes everything we don't need from rufus.loc, for embedding.

# remove comments (but not so aggressively as to drop the end of MSG_298!)
s/^#.*$//
s/[ \t]#.*$//

# remove empty lines
/^$/d

# remove trailing whitespaces
s/[ \t]*$//

# remove the UI controls for "en-US" as they are just here for translators
# 1,300 means we only do this for the the first 300 lines
1,300 {/^g IDD_DIALOG/,/^t MSG_001/{/^t MSG_001/!d}}

# remove the Windows AppStore specific messages
/MSG_9/d

# output file *MUST* be CR/LF
s/$/\r/