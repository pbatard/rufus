Rufus: The Reliable USB Formatting Utility - Windows 11 setup.exe wrapper

# Description

This small executable aims at solving the issue of Windows 11 24H2 having made the
bypass requirements for in-place upgrades more difficult to enact.

Basically, per https://github.com/pbatard/rufus/issues/2568#issuecomment-2387934171,
and if the user chose to apply the hardware requirement bypasses in Rufus, you want
to apply a set of registry key creation and deletion *before* setup.exe is run.

While we could obviously provide a simple batch file to accomplish this, the fact
that the registry commands require elevation, combined with expectations of just
being able double click setup.exe to upgrade makes us want to accomplish this in
a more user-friendly manner.

Our solution then is to have Rufus rename the original 'setup.exe' to 'setup.dll'
insert a small 'setup.exe' that'll perform elevation, add the registry key, and
launch the original setup, which is exactly what this project does.

Now, obviously, the fact that we "inject" a setup executable may leave people
uncomfortable about the possibility that we might use this as a malware vector,
which is also why we make sure that the one we sign and embed in Rufus does get
built using GitHub Actions and can be validated to not have been tampered through
SHA-256 validation (Since we produce SHA-256 hashes during the build process per:
https://github.com/pbatard/rufus/blob/master/.github/workflows/setup.yml).

Also note that, since these are the only platforms Windows 11 supports, we only
build for x64 and ARM64.
