# PowerShell script to parse listing.csv and retrieve our screenshots
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
