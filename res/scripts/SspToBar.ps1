#
# SspToBar.ps1 - SkuSiPolicy.p7b revoked PE256 hashes to C byte array converter
# Copyright © 2023 Pete Batard <pete@akeo.ie>
# Heavily derived from https://gist.github.com/mattifestation/92e545bf1ee5b68eeb71d254cec2f78e
# Copyright © 2016-2019 Matthew Graeber with contributions by James Forshaw
#
# License: BSD 3-Clause
#

# This script is generates the pe256ssp[] byte array from Rufus' db.h

#region Parameters
[cmdletbinding()]
param(
	# (Optional) The path to the .p7b to process
	[string]$BinaryFilePath = "SkuSiPolicy.p7b",
	# (Optional) Output the straight values
	[switch]$Raw = $false
)
#endregion

Add-Type -AssemblyName 'System.Security'

$BinPath = Resolve-Path $BinaryFilePath
$GuidLength = 0x10
$Pe256HashLength = 0x20
$HeaderLengthMax = 0x44

# Helper function to read strings from the binary
function Get-BinaryString {
	[OutputType('String')]
	param (
		[Parameter(Mandatory)]
		[IO.BinaryReader]
		[ValidateNotNullOrEmpty()]
		$BinaryReader
	)

	$StringLength = $BinaryReader.ReadUInt32()

	if ($StringLength) {
		$PaddingBytes = 4 - $StringLength % 4 -band 3

		$StringBytes = $BinaryReader.ReadBytes($StringLength)
		$null = $BinaryReader.ReadBytes($PaddingBytes)

		[Text.Encoding]::Unicode.GetString($StringBytes)
	}

	$null = $BinaryReader.ReadInt32()
}

try {
	$CIPolicyBytes = [IO.File]::ReadAllBytes($BinPath.Path)

	try {
		$ContentType = $null
		try {
			$ContentType = [Security.Cryptography.Pkcs.ContentInfo]::GetContentType($CIPolicyBytes)
		} catch { }

		# Check for PKCS#7 ASN.1 SignedData type
		if ($ContentType -and $ContentType.Value -eq '1.2.840.113549.1.7.2') {
		  $Cms = New-Object System.Security.Cryptography.Pkcs.SignedCms
		  $Cms.Decode($CIPolicyBytes)
		  $CIPolicyBytes = $Cms.ContentInfo.Content
		  if ($CIPolicyBytes[0] -eq 4) {
			$PolicySize = $CIPolicyBytes[1]
			$BaseIndex = 2
			if (($PolicySize -band 0x80) -eq 0x80) {
				$SizeCount = $PolicySize -band 0x7F
				$BaseIndex += $SizeCount
				$PolicySize = 0
				for ($i = 0; $i -lt $SizeCount; $i++) {
					$PolicySize = $PolicySize -shl 8
					$PolicySize = $PolicySize -bor $CIPolicyBytes[2 + $i]
				}
			}

			$CIPolicyBytes = $CIPolicyBytes[$BaseIndex..($BaseIndex + $PolicySize - 1)]
		  }
		}
	} catch {
		Write-Output $_
	}

	$MemoryStream = New-Object -TypeName IO.MemoryStream -ArgumentList @(,$CIPolicyBytes)
	$BinaryReader = New-Object -TypeName System.IO.BinaryReader -ArgumentList $MemoryStream, ([Text.Encoding]::Unicode)
} catch {
	throw $_
	return
}

try {
	$CIPolicyFormatVersion = $BinaryReader.ReadInt32()
	Write-Verbose "Detected CI Policy Format Version $CIPolicyFormatVersion"
	if ($CIPolicyFormatVersion -gt 7) {
		Write-Warning "CI Policy Format may be unsupported..."
	}

	$PolicyTypeID = [Guid][Byte[]] $BinaryReader.ReadBytes($GuidLength)
	switch ($PolicyTypeID.Guid) {
		'a244370e-44c9-4c06-b551-f6016e563076' { Write-Verbose "Policy Type {$PolicyTypeID} => Enterprise Code Integrity Policy (SiPolicy.p7b or UpdateSiPolicy.p7b)" }
		'2a5a0136-f09f-498e-99cc-51099011157c' { Write-Verbose "Policy Type {$PolicyTypeID} => Windows Revoke Code Integrity Policy (RvkSiPolicy.p7b or UpdateRvkSiPolicy.p7b)" }
		'976d12c8-cb9f-4730-be52-54600843238e' { Write-Verbose "Policy Type {$PolicyTypeID} => SKU Code Integrity Policy (SkuSiPolicy.p7b or UpdateSkuSiPolicy.p7b)" }
		'5951a96a-e0b5-4d3d-8fb8-3e5b61030784' { Write-Verbose "Policy Type {$PolicyTypeID} => Windows Lockdown Code Integrity Policy (WinSiPolicy.p7b or UpdateWinSiPolicy.p7b)" }
		'4e61c68c-97f6-430b-9cd7-9b1004706770' { Write-Verbose "Policy Type {$PolicyTypeID} => Advanced Threat Protection Code Integrity Policy (ATPSiPolicy.p7b or UpdateATPSiPolicy.p7b)" }
		'd2bda982-ccf6-4344-ac5b-0b44427b6816' { Write-Verbose "Policy Type {$PolicyTypeID} => Driver Code Integrity Policy (DriverSiPolicy.p7b or UpdateDriverSiPolicy.p7b)" }
		default { Write-Warning "Policy Type {$PolicyTypeID} => Unknown Policy Type" }
	}

	[Byte[]] $PlatformIDBytes = $BinaryReader.ReadBytes($GuidLength)
	$PlatformID = [Guid] $PlatformIDBytes
	Write-Verbose "PlatformID: {$PlatformID}"
	
	$OptionFlags = $BinaryReader.ReadInt32()
	Write-Verbose "Policy Option Flags: 0x$($OptionFlags.ToString('X8'))"

	if ($OptionFlags -band ([Int32]::MinValue) -ne [Int32]::MinValue) {
		throw "Invalid Policy Option Flags"
		return
	}
	if (($OptionFlags -band 0x40000000) -eq 0x40000000) {
		Write-Warning 'Policy Option Flags indicate that the CI Policy was built from supplemental policies.'
	}

	$EKURuleEntryCount = $BinaryReader.ReadInt32()
	Write-Verbose "$EKURuleEntryCount EKU Rule(s)"

	$FileRuleEntryCount = $BinaryReader.ReadInt32()
	Write-Verbose "$FileRuleEntryCount File Rule(s)"

	$SignerRuleEntryCount = $BinaryReader.ReadInt32()
	Write-Verbose "$SignerRuleEntryCount Signer Rule(s)"

	$SignerScenarioEntryCount = $BinaryReader.ReadInt32()
	Write-Verbose "$SignerScenarioEntryCount Signer Scenario(s)"

	$Revis = $BinaryReader.ReadUInt16()
	$Build = $BinaryReader.ReadUInt16()
	$Minor = $BinaryReader.ReadUInt16()
	$Major = $BinaryReader.ReadUInt16()
	Write-Verbose "Version: $Major.$Minor.$Build.$Revis"

	$HeaderLength = $BinaryReader.ReadInt32()	
	if ($HeaderLength -ne ($HeaderLengthMax - 4)) {
		Write-Warning "$BinPath has an invalid header footer: 0x$($HeaderLength.ToString('x8'))"
	}

	if ($EKURuleEntryCount) {
		Write-Verbose "Skipping EKU Rules..."
		for ($i = 0; $i -lt $EKURuleEntryCount; $i++) {
			$EkuValueLen = $BinaryReader.ReadUInt32()
			$PaddingBytes = 4 - $EkuValueLen % 4 -band 3
			$null = $BinaryReader.ReadBytes($EkuValueLen)
			$null = $BinaryReader.ReadBytes($PaddingBytes)
		}
	}
	
	if ($FileRuleEntryCount) {
		Write-Verbose "Processing File Rules..."
		$HashArray = New-Object System.Collections.ArrayList
		
		for ($i = 0; $i -lt $FileRuleEntryCount; $i++) {
			$FileRuleTypeValue = $BinaryReader.ReadInt32()

			$FileName = Get-BinaryString -BinaryReader $BinaryReader
			$Revis = $BinaryReader.ReadUInt16()
			$Build = $BinaryReader.ReadUInt16()
			$Minor = $BinaryReader.ReadUInt16()
			$Major = $BinaryReader.ReadUInt16()

			$HashLen = $BinaryReader.ReadUInt32()
			if ($HashLen) {
				$PaddingBytes = 4 - $HashLen % 4 -band 3
				$HashBytes = $BinaryReader.ReadBytes($HashLen)
				# We are only interested in the 'DENY' type (0) for PE256 hashes
				if (($FileRuleTypeValue -eq 0) -and ($HashLen -eq $Pe256HashLength)) {
					$HashString = ($HashBytes | ForEach-Object ToString x2) -join ''
					$HashArray.Add($HashString) | Out-Null
				}
				$null = $BinaryReader.ReadBytes($PaddingBytes)
			}
		}
		# Sort the array and remove duplicates
		$HashArray.Sort()
		$HashArray = $HashArray | Select-Object -Unique
		foreach ($HashStr in $HashArray) {
			if ($Raw) {
				Write-Output $HashStr
			} else {
				$HashChars = $HashStr.ToCharArray()
				$Line = "`t"
				for ($i = 0; $i -lt $Pe256HashLength; $i++) {
					$Line += "0x" + $HashChars[2 * $i] + $HashChars[2 * $i + 1] + ", "
				}
				Write-Output $Line
			}
		}
	}
	
} catch {
	$BinaryReader.Close()
	$MemoryStream.Close()

	throw $_
	return
}
