# verify-windows-msvc.ps1 - Windows/MSVC smoke verification for CC Forge.
#
# Run from a Windows host with Visual Studio Build Tools installed. Pass
# -Vcvars for non-standard installations, or set FORGE_WINDOWS_VC_VARS /
# FORGE_WINDOWS_VS_ROOT in the environment.
#
# This is intentionally a smoke gate, not the full native matrix. It covers the
# std::execution backport, unique_resource, and non-Linux forge:: runtime
# utilities. Linux-only IO is disabled.

[CmdletBinding()]
param(
    [string]$SourcePath = "",
    [string]$Repo = "https://github.com/kalcohol/ccforge.git",
    [string]$Ref = "master",
    [string]$VisualStudioRoot = "",
    [string]$VsVersion = "18",
    [string]$Vcvars = "",
    [string]$BuildName = "",
    [string]$CTestRegex = "execution|unique_resource|forge",
    [switch]$Keep,
    [switch]$SkipGoogletestProvision
)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command
    )

    Write-Host "[msvc] $Label"
    cmd /s /c $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($Label) with exit code $LASTEXITCODE"
    }
}

function Find-ScriptRepoRoot {
    if ($SourcePath) {
        return (Resolve-Path $SourcePath).Path
    }

    if ($PSScriptRoot) {
        $candidate = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
        if ((Test-Path (Join-Path $candidate "CMakeLists.txt")) -and
            (Test-Path (Join-Path $candidate "forge.cmake"))) {
            return $candidate
        }
    }

    return ""
}

function Resolve-Vcvars {
    if ($Vcvars) {
        return (Resolve-Path $Vcvars).Path
    }

    if ($env:FORGE_WINDOWS_VC_VARS) {
        return (Resolve-Path $env:FORGE_WINDOWS_VC_VARS).Path
    }

    $roots = @()
    if ($VisualStudioRoot) {
        $roots += $VisualStudioRoot
    } elseif ($env:FORGE_WINDOWS_VS_ROOT) {
        $roots += $env:FORGE_WINDOWS_VS_ROOT
    } else {
        if ($env:ProgramFiles) {
            $roots += (Join-Path $env:ProgramFiles "Microsoft Visual Studio")
        }
        if (${env:ProgramFiles(x86)}) {
            $roots += (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio")
        }
    }

    foreach ($root in $roots) {
        $candidate = Join-Path $root "$VsVersion\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $vswhere = ""
    if (${env:ProgramFiles(x86)}) {
        $candidate = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $candidate) {
            $vswhere = $candidate
        }
    }

    if ($vswhere) {
        $install = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
        if ($install) {
            $candidate = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) {
                return (Resolve-Path $candidate).Path
            }
        }
    }

    throw "vcvars64.bat not found. Pass -Vcvars or set FORGE_WINDOWS_VC_VARS / FORGE_WINDOWS_VS_ROOT."
}

$sourceRoot = Find-ScriptRepoRoot
$workRoot = Join-Path $env:TEMP ("ccforge-win-msvc-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$cloned = $false
$success = $false

try {
    if (-not $sourceRoot) {
        New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
        $sourceRoot = Join-Path $workRoot "src"
        Invoke-Native "clone $Repo" "git clone `"$Repo`" `"$sourceRoot`""
        $cloned = $true
        if ($Ref) {
            Invoke-Native "checkout $Ref" "cd /d `"$sourceRoot`" && git checkout `"$Ref`""
        }
    }

    if (-not (Test-Path (Join-Path $sourceRoot "CMakeLists.txt"))) {
        throw "SourcePath does not look like the CC Forge repo: $sourceRoot"
    }

    $gtestDir = Join-Path $sourceRoot "3rdparty\googletest"
    if ((-not (Test-Path $gtestDir)) -and (-not $SkipGoogletestProvision)) {
        New-Item -ItemType Directory -Force -Path (Join-Path $sourceRoot "3rdparty") | Out-Null
        Invoke-Native "provision googletest" "git clone --depth 1 `"https://github.com/google/googletest.git`" `"$gtestDir`""
    }

    if (-not (Test-Path $gtestDir)) {
        throw "Missing 3rdparty\googletest. Re-run without -SkipGoogletestProvision or provide it manually."
    }

    $Vcvars = Resolve-Vcvars

    if (-not $BuildName) {
        $BuildName = "msvc-$VsVersion-smoke"
    }
    $buildDir = Join-Path $sourceRoot "build\$BuildName"
    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
    }

    Write-Host "[msvc] source=$sourceRoot"
    Write-Host "[msvc] build=$buildDir"
    Write-Host "[msvc] vcvars=$Vcvars"

    $common = "call `"$Vcvars`" >nul && "
    $configure =
        $common +
        "cmake -S `"$sourceRoot`" -B `"$buildDir`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=ON " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_FORGE_IO=OFF " +
        "-DFORGE_TEST_ENABLE_SIMD=OFF " +
        "-DFORGE_TEST_ENABLE_SUBMDSPAN=OFF " +
        "-DFORGE_TEST_ENABLE_LINALG=OFF " +
        "-DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF"
    $build = $common + "cmake --build `"$buildDir`""
    $test = $common + "ctest --test-dir `"$buildDir`" -R `"$CTestRegex`" --output-on-failure"

    Invoke-Native "configure" $configure
    Invoke-Native "build" $build
    Invoke-Native "test" $test

    $success = $true
    Write-Host "[msvc] verified"
} finally {
    if ($success -and $cloned -and (-not $Keep)) {
        Remove-Item -Recurse -Force $workRoot
    } elseif ($cloned) {
        Write-Host "[msvc] kept work root: $workRoot"
    }
}
