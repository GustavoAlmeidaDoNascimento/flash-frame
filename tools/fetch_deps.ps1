$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Third = Join-Path $Root "third_party"
New-Item -ItemType Directory -Force -Path $Third | Out-Null

function Get-File([string]$Url, [string]$Dest) {
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
}

$MinizFiles = @(
    "miniz.h", "miniz.c", "miniz_common.h", "miniz_tdef.h", "miniz_tdef.c",
    "miniz_tinfl.h", "miniz_tinfl.c", "miniz_zip.h", "miniz_zip.c"
)
if (-not (Test-Path (Join-Path $Third "miniz_zip.h"))) {
    foreach ($name in $MinizFiles) {
        Get-File "https://raw.githubusercontent.com/richgel999/miniz/2.2.0/$name" (Join-Path $Third $name)
    }
}
if (-not (Test-Path (Join-Path $Third "miniz_export.h"))) {
    Set-Content -Path (Join-Path $Third "miniz_export.h") -Value "#pragma once`r`n#ifndef MINIZ_EXPORT`r`n#define MINIZ_EXPORT`r`n#endif`r`n"
}

if (-not (Test-Path (Join-Path $Third "cJSON.c"))) {
    Get-File "https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.h" (Join-Path $Third "cJSON.h")
    Get-File "https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.c" (Join-Path $Third "cJSON.c")
}

$HaveSqlite = Get-ChildItem -Path $Third -Recurse -Filter sqlite3.c -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $HaveSqlite) {
    $page = Invoke-WebRequest -Uri "https://www.sqlite.org/download.html" -UseBasicParsing
    $match = [regex]::Match($page.Content, "sqlite-amalgamation-(\d+)\.zip")
    if (-not $match.Success) {
        throw "Could not find the SQLite amalgamation on sqlite.org"
    }
    $version = $match.Groups[1].Value
    $zip = Join-Path $Third "sqlite-amalgamation.zip"
    $downloaded = $false
    foreach ($year in 2026, 2025, 2024) {
        $url = "https://www.sqlite.org/$year/sqlite-amalgamation-$version.zip"
        try {
            Get-File $url $zip
            $downloaded = $true
            break
        } catch {
            Write-Host "miss $url"
        }
    }
    if (-not $downloaded) {
        throw "Could not download sqlite-amalgamation-$version.zip"
    }
    Expand-Archive -Path $zip -DestinationPath $Third -Force
    Remove-Item $zip
}

$HaveZstd = Get-ChildItem -Path $Third -Directory -Filter "zstd-*" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $HaveZstd) {
    $tar = Join-Path $Third "zstd.tar.gz"
    Get-File "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz" $tar
    tar -xzf $tar -C $Third
    Remove-Item $tar
}

Write-Host "Dependencies are in $Third"
