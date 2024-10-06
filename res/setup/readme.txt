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

Per the https://github.com/pbatard/rufus/actions/runs/11195726475 GitHub Actions
workflow run, the SHA-256 for the executables (before signature was applied) were:
* 4e99f49b456781c92d2010a626706557df69334c6fc71ac129625f41fa311dd8 *./setup_x64.exe
* a0d7dea2228415eb5afe34419a31eeda90f9b51338f47bc8a5ef591054277f4b *./setup_arm64.exe

You will also find the VirusTotal reports for the current signed executable at:
* https://www.virustotal.com/gui/file/a74dbfc0e2a5b2e3fd4ad3f9fdaea0060c5d29a16151b9a570a9bd653db966a7/detection
* https://www.virustotal.com/gui/file/629fdda27e200ec98703a7606ca4e2d0e44c578341779e4d5c447d45fc860c69/detection
