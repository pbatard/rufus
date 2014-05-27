Rufus: The Reliable USB Formatting Utility - Commandline hogger

# Description

This little utility is intended to solve the issue of Windows applications not
being able to be BOTH console and GUI [1], leading to all kind of annoyances [2].

The basic problem is as follows:

1. You have an awesome Windows UI application that you want your users to be
   able to start from the commandline (to provide parameters, etc.)
2. If an application is set to operate in UI mode, then when you launch it from
   the command line, you will NOT get a command prompt freeze until it exits, as
   you'd expect from any console app, but instead the prompt will return to the
   user right away.
3. This means that any message that you try to output from your app to the
   console will appear as ugly as:
   C:\Some\Directory> Some Application Message
4. Another unfortunate effect is that, when users exit your app, they might
   continue to wait for the prompt to come back, when it is already available
   whilst polluted by output that looks out of place => Your user experience is
   now subpar...
5. To compensate for this, you might try to be (somewhat) clever, through the
   simulating an <Enter> keypress when your app exit, but lo and behold, soon
   enough you start receiving complaints left and right from Far Manager [3]
   users, due to the fact that this application uses the <Enter> keypress as an
   "I want to launch the currently selected app" event.
6. HEY, MICROSOFT, THIS SUPER-SUCKS!!!!

# Primer

So, how far do we need to go to address this?

1. We'll create a console hogger application, that does just what you'd expect
   a regular commandline app to do (hog the console prompt until the app exits)
   and wait for a signal (Mutex) from the UI app to indicate that it is closed.
2. We'll embed this console hogger as a resource in our app, to be extracted
   and run in the current directory whenever we detect that our UI app has
   been launched from the commandline
3. Because we want this annoyance to have the least impact possible, we'll
   make sure that it is AS SMALL AS POSSIBLE, by writing it in pure assembly
   so that we can compile it with NASM and linking it with WDK, leaving us
   with a 2 KB executable (that will further be compressed to about 1/4th of
   this size through UPX/LZMA).

# Drawbacks

The one annoyance with this workaround is that our 'hogger' will appear in the
command history (doskey). This means that when when a user wants to navigate
back to the command they launched, they need to skip through an extra entry,
which they probably have no idea about. And of course, doskey does not provide
the ability to suppress this last entry.

Oh, and you also need to release the mutex so that you can delete the file 
before you exit, though that's not a big deal...

# Compilation

; From a WDK command prompt

nasm -fwin32 hogger.asm

link /subsystem:console /nodefaultlib /entry:main hogger.obj %SDK_LIB_DEST%\i386\kernel32.lib

# Links

[1] http://blogs.msdn.com/b/oldnewthing/archive/2009/01/01/9259142.aspx
[2] https://github.com/pbatard/rufus/issues/161
[3] http://www.farmanager.com/
