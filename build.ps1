$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$Build = Join-Path $Root "build"
New-Item -ItemType Directory -Force -Path $Build | Out-Null

function Invoke-Vc([string]$Command) {
    cmd /c "call `"$Vcvars`" >nul && cd /d `"$Root`" && $Command"
    if ($LASTEXITCODE -ne 0) {
        throw "Build command failed ($LASTEXITCODE)"
    }
}

$Target = "tests"
if ($args.Count -gt 0) {
    $Target = $args[0]
}

$Core = @(
    "src\util.c",
    "src\scheduler.c",
    "src\textutil.c",
    "src\proto.c",
    "src\budget.c"
)

if ($Target -eq "tests" -or $Target -eq "all") {
    $Files = ($Core + @("tests\test_core.c") | ForEach-Object { "`"$_`"" }) -join " "
    Invoke-Vc "cl /nologo /W3 /O2 /std:c17 /utf-8 /D_CRT_SECURE_NO_WARNINGS /Isrc /Fo`"$Build\\`" /Fe`"$Build\fftests.exe`" $Files"
    & (Join-Path $Build "fftests.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "Core tests failed"
    }
}

function Get-EngineFiles {
    $Third = Join-Path $Root "third_party"
    $Sqlite = Get-ChildItem -Path $Third -Recurse -Filter sqlite3.c -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -like "sqlite-amalgamation*" } |
        Select-Object -First 1
    if (-not $Sqlite -or -not (Test-Path (Join-Path $Third "miniz_zip.c"))) {
        & (Join-Path $Root "tools\fetch_deps.ps1")
        $Sqlite = Get-ChildItem -Path $Third -Recurse -Filter sqlite3.c |
            Where-Object { $_.Directory.Name -like "sqlite-amalgamation*" } |
            Select-Object -First 1
    }
    $ZstdRoot = Get-ChildItem -Path $Third -Directory -Filter "zstd-*" | Select-Object -First 1
    if (-not $Sqlite -or -not $ZstdRoot) {
        throw "Missing third_party sources. Run tools\fetch_deps.ps1"
    }
    $script:SqliteDir = $Sqlite.DirectoryName
    $script:ZstdInclude = Join-Path $ZstdRoot.FullName "lib"
    $sources = @(
        "src\util.c", "src\scheduler.c", "src\textutil.c", "src\proto.c", "src\budget.c",
        "src\path.c", "src\storage.c", "src\anki.c", "src\anki_schema.c", "src\apkg.c",
        "third_party\miniz.c", "third_party\miniz_tdef.c", "third_party\miniz_tinfl.c", "third_party\miniz_zip.c",
        "third_party\cJSON.c", $Sqlite.FullName
    )
    $zstd = @(Get-ChildItem (Join-Path $ZstdRoot.FullName "lib\common\*.c")) +
        @(Get-ChildItem (Join-Path $ZstdRoot.FullName "lib\decompress\*.c"))
    return @{ Sources = $sources; Zstd = $zstd }
}

if ($Target -eq "import" -or $Target -eq "app" -or $Target -eq "all") {
    $Engine = Get-EngineFiles
    $Includes = "/Isrc /Ithird_party /I`"$SqliteDir`" /I`"$ZstdInclude`""
    $Defines = "/D_CRT_SECURE_NO_WARNINGS /DSQLITE_THREADSAFE=0 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_DQS=0 /DZSTD_DISABLE_ASM=1 /DZSTD_LEGACY_SUPPORT=0"
    $Quoted = ($Engine.Sources | ForEach-Object { "`"$_`"" }) -join " "
    $ZQuoted = ($Engine.Zstd | ForEach-Object { "`"$($_.FullName)`"" }) -join " "
    if ($Target -eq "import" -or $Target -eq "all") {
        Invoke-Vc "cl /nologo /W3 /O2 /std:c17 /utf-8 $Defines $Includes /Fo`"$Build\\`" /Fe`"$Build\test_import.exe`" $Quoted $ZQuoted `"tests\test_import.c`" /link"
        & (Join-Path $Build "test_import.exe")
        if ($LASTEXITCODE -ne 0) {
            throw "Import tests failed"
        }
    }
    if ($Target -eq "app" -or $Target -eq "all") {
        $Win = @(
            "src\win\main.c", "src\win\ui.c", "src\win\modal.c", "src\win\dialogs.c", "src\win\card_dialog.c", "src\win\tray.c", "src\win\media.c"
        )
        $WinQuoted = ($Win | ForEach-Object { "`"$_`"" }) -join " "
        $Libs = "user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib comdlg32.lib psapi.lib winmm.lib windowscodecs.lib advapi32.lib uuid.lib"
        Invoke-Vc "cl /nologo /W3 /O2 /std:c17 /utf-8 /DUNICODE /D_UNICODE $Defines $Includes /Fo`"$Build\\`" /Fe`"$Build\flash-frame.exe`" $Quoted $ZQuoted $WinQuoted /link /SUBSYSTEM:WINDOWS $Libs"
        Write-Host "Built $Build\flash-frame.exe"
    }
}
