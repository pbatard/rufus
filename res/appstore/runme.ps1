# PowerShell script to parse listing.csv and retrieve our screenshots
# Copyright © 2023 Pete Batard <pete@akeo.ie>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
try {
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

# NB: All languages IDs from the .csv are lowercase version of the one
# from rufus.loc, except for 'sr-RS' that becomes 'sr-latn-rs'. 

function GetCellByName([object]$csv, [string]$row_name, [string]$column_name)
{
  foreach ($row in $csv | Where-Object {$_.Field -eq $row_name}) {
    foreach ($column in $row.PSObject.properties) {
      if ($column.name -eq $column_name) {
        return $column.value
      }
    }
  }
  return [string]::Empty
} 

$csv = Import-Csv -Path .\listing.csv
$langs = $csv | Select-Object -First 1 | Select * -ExcludeProperty 'Field','ID','Type (Type)','default' | ForEach-Object { $_.PSObject.Properties } | Select-Object -ExpandProperty Name

foreach ($lang in $langs) {
  $null = New-Item $lang -ItemType Directory -Force
  $url = GetCellByName $csv 'DesktopScreenshot1' $lang
  # Annoying but heck if I'm gonna bother with Microsoft's Auth in PowerShell...
  Start-Process -NoNewWindow -FilePath "C:\Program Files\Mozilla Firefox\firefox.exe" -ArgumentList "-new-tab $url"
  Write-Host $lang;
  $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown');
}
