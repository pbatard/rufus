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

Per the https://github.com/pbatard/rufus/actions/runs/15539519674 GitHub Actions
workflow run, the SHA-256 for the executables (before signature was applied) were:
* bea9a68e73b53820ea9c3fcdda70b1615f7453ad483dd3aa06e0385fd28f004e *./setup_x64.exe
* f024aed61b1f215a3b684bbb2fd176893fc61f92243708a0d9334671df1cdb61 *./setup_arm64.exe

You will also find the VirusTotal reports for the current signed executable at:
* https://www.virustotal.com/gui/file/41037f63f20f5984e5ca1dab12c033d795966e80bd8b76a39c1dc42a3dc33594/detection
* https://www.virustotal.com/gui/file/cc9cb4f4080db352d9a82ef926fc290d62a8d67a45a5557f46fa924a7b9bdbe3/detection
