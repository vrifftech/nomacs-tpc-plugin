param(
    [Parameter(Mandatory = $true)]
    [string]$Platform,

    [string]$BuildDirectory = "build",

    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Platform -ne "windows-x86_64") {
    throw "Unsupported platform: $Platform"
}

$candidates = @(
    Get-ChildItem -Path $BuildDirectory -Recurse -File -Filter "qtpc.dll" |
        Where-Object { $_.FullName -notmatch "CMakeFiles" }
)

if ($candidates.Count -eq 0) {
    throw "qtpc.dll was not found below $BuildDirectory"
}

$releaseCandidates = @(
    $candidates | Where-Object { $_.FullName -match "[\\/]Release[\\/]" }
)

if ($releaseCandidates.Count -eq 1) {
    $plugin = $releaseCandidates[0]
} elseif ($candidates.Count -eq 1) {
    $plugin = $candidates[0]
} else {
    $paths = ($candidates.FullName -join [Environment]::NewLine)
    throw "More than one qtpc.dll candidate was found:$([Environment]::NewLine)$paths"
}

$packageName = "qt-tpc-image-plugin-$Platform"
$stageParent = Join-Path $OutputDirectory "stage"
$stageDirectory = Join-Path $stageParent $packageName
$imageFormatsDirectory = Join-Path $stageDirectory "imageformats"
$archivePath = Join-Path $OutputDirectory "$packageName.zip"

if (Test-Path $stageDirectory) {
    Remove-Item -Recurse -Force $stageDirectory
}

New-Item -ItemType Directory -Force -Path $imageFormatsDirectory | Out-Null
Copy-Item $plugin.FullName -Destination $imageFormatsDirectory

foreach ($file in @("README.md", "LICENSE", "LICENSE.txt")) {
    if (Test-Path $file -PathType Leaf) {
        Copy-Item $file -Destination $stageDirectory
    }
}

$qtVersion = if ($env:QT_VERSION) { $env:QT_VERSION } else { "unknown" }
$sourceCommit = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { "local-build" }

@"
Qt TPC image-format plugin

Platform: $Platform
Qt build version: $qtVersion
Source commit: $sourceCommit

Copy imageformats\qtpc.dll into the imageformats directory beside nomacs.exe,
then restart nomacs. The Qt major version, CPU architecture, and release build
configuration must match the target application.
"@ | Set-Content -Path (Join-Path $stageDirectory "INSTALL.txt") -Encoding UTF8

if (Test-Path $archivePath) {
    Remove-Item -Force $archivePath
}

Compress-Archive -Path $stageDirectory -DestinationPath $archivePath -CompressionLevel Optimal
Remove-Item -Recurse -Force $stageParent

Write-Host "Created $archivePath"
