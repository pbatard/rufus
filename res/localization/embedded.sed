# This sed script removes everything we don't need from rufus.loc, for embedding.

# remove comments (aggressively!)
s/#.*$//

# remove empty lines
/^$/d

# remove trailing whitespaces
s/[ \t]*$//

# remove the UI controls for "en-US" as they are just here for translators
# 1,300 means we only do this for the the first 300 lines
1,300 {/^g IDD_DIALOG/,/^t MSG_001/{/^t MSG_001/!d}}

# also replace the "translated by" line for English
1,500{s/^t MSG_176.*$/t MSG_176 \"mailto:pete@akeo.ie\"/}

# output file *MUST* be CR/LF
s/$/\r/