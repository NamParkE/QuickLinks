# Starts a test instance of QuickLinks on a copy of tests\sample.ini, runs steps, captures the widget
# window only, and closes the instance. The user's own list (%APPDATA%\QuickLinks) is never touched.
#
#   & tests\run_ui.ps1 -Out <dir> [-Ini tests\sample.ini] [-Theme light|dark] [-Steps "capture:base","key:40","capture:selected"]
#
# Steps: capture:<name> | capslider:<name> | capsettings:<name> | key:<virtual-key> | type:<text> | cmd:<command id> | wait:<ms>
# Avoid cmd:112 (paste): it reads the user's real clipboard.
param(
    [Parameter(Mandatory)] [string] $Out,
    [string] $Ini = "$PSScriptRoot\sample.ini",
    [string] $Theme = '',
    [switch] $Win10Fonts,  # draw with the Windows 10 fonts (Segoe UI / Segoe MDL2 Assets)
    [string[]] $Steps = @('capture:base')
)
$Exe = "$PSScriptRoot\..\build\QuickLinks.exe"
$ErrorActionPreference = 'Stop'
# qltool is a test build: made here and deleted again at the end.
$tool = "$PSScriptRoot\..\build\qltool.exe"
if (-not (Test-Path $tool)) { cmd /c "$PSScriptRoot\..\tools\build_qltool.bat" | Out-Null }
New-Item -ItemType Directory -Force $Out | Out-Null

# The ini must be UTF-16 for GetPrivateProfileString; the sample is kept as UTF-8 in the repo.
$work = Join-Path $Out 'test.ini'
Get-Content -Raw -Encoding utf8 $Ini | Set-Content -Encoding unicode -NoNewline $work

$args = @('--config', "`"$work`"")
if ($Theme) { $args += @('--theme', $Theme) }
if ($Win10Fonts) { $args += '--win10-fonts' }
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru
try {
    Start-Sleep -Milliseconds 2500
    foreach ($s in $Steps) {
        $kind, $arg = $s -split ':', 2
        switch ($kind) {
            'capture' { & $tool capture (Join-Path $Out "$arg.png") }
            'capslider' { & $tool capture (Join-Path $Out "$arg.png") slider }
            'capsettings' { & $tool capture (Join-Path $Out "$arg.png") settings }
            'key' { & $tool key $arg; Start-Sleep -Milliseconds 300 }
            'type' { & $tool type $arg; Start-Sleep -Milliseconds 300 }
            'cmd' { & $tool cmd $arg; Start-Sleep -Milliseconds 400 }
            'wait' { Start-Sleep -Milliseconds ([int]$arg) }
        }
    }
} finally {
    & $tool quit | Out-Null
    if (-not $p.WaitForExit(5000)) { $p.Kill() }
    [IO.File]::Delete($tool)
    [IO.File]::Delete("$PSScriptRoot\..\build\obj\qltool.obj")
}
