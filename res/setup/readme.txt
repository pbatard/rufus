Rufus: The Reliable USB Formatting Utility - Windows 11 Setup wrapper

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

Oh and it should be noted that, the issues you might see with Setup not restarting
in the foreground after it updates, or not being able to launch at all for a while
if you happen to cancel before starting the installation, have *NOTHING* to do with
using this setup wrapper, but come from Microsoft themselves. You can validate that
these issues exist even when running setup.exe without the wrapper...

# Security considerations

Obviously, the fact that we "inject" a setup executable may leave people uncomfortable
about the possibility that we might use this as a malware vector, which is also why we
make sure that the one we sign and embed in Rufus does get built using GitHub Actions
and can be validated to not have been tampered through SHA-256 validation (Since we
produce SHA-256 hashes during the build process per:
https://github.com/pbatard/rufus/blob/master/.github/workflows/setup.yml).

Per the https://github.com/pbatard/rufus/actions/runs/16191913388 GitHub Actions
workflow run, the SHA-256 for the executables (before signature was applied) were:
* f8e1c7c5f1297be7a76d73567d4d82f61bb20c2e5c86d2a2f8d2e5961751d658 *./setup_x64.exe
* e6ff77b859231cc58c872c7b14ce9def73244641e487bbb074d3a759bdfcbc8d *./setup_arm64.exe

You will also find the VirusTotal reports for the current signed executable at:
* https://www.virustotal.com/gui/file/11df838dc69378187e1e1aaf32d34384157642d07096c6e49c1d0e7375634544/detection
* https://www.virustotal.com/gui/file/14bd07f559513890a0f6565df3927392b4fe6b8e6fc3f5e832e9d69c8b7bb7eb/detection
