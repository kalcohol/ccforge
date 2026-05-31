# verify-windows-msvc.ps1 - Windows/MSVC smoke verification for CC Forge.
#
# Run from a Windows host with Visual Studio Build Tools installed. Pass
# -Vcvars for non-standard installations, or set FORGE_WINDOWS_VC_VARS /
# FORGE_WINDOWS_VS_ROOT in the environment.
#
# This is intentionally a smoke gate, not the full native matrix. It covers the
# std::execution backport, unique_resource, forge:: runtime utilities, and the
# Windows forge::io backend when available.

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
    [switch]$SkipGoogletestProvision,
    [switch]$SkipGateChecks,
    [switch]$SkipInstallPackageCheck
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

function Invoke-NativeOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command
    )

    Write-Host "[msvc] $Label"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = cmd /s /c $Command 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) {
        throw "Command failed ($Label) with exit code $exitCode"
    }
    return ,$output
}

function Invoke-NativeCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command
    )

    Write-Host "[msvc] $Label"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = cmd /s /c $Command 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    $output | ForEach-Object { Write-Host $_ }
    return @{
        ExitCode = $exitCode
        Output = $output
    }
}

function Assert-OutputContains {
    param(
        [Parameter(Mandatory = $true)][object[]]$Output,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $text = ($Output -join "`n")
    if (-not $text.Contains($Needle)) {
        throw "$Label did not contain expected text: $Needle"
    }
}

function Get-CtestCount {
    param([Parameter(Mandatory = $true)][object[]]$Output)

    $text = ($Output -join "`n")
    $match = [regex]::Match($text, "Total Tests:\s+(\d+)")
    if (-not $match.Success) {
        throw "Could not parse ctest -N test count"
    }
    return [int]$match.Groups[1].Value
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

function Invoke-GateChecks {
    param(
        [Parameter(Mandatory = $true)][string]$Common,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$BuildName
    )

    $autoBuild = Join-Path $SourceRoot "build\$BuildName-io-auto"
    $onBuild = Join-Path $SourceRoot "build\$BuildName-io-on"
    $offBuild = Join-Path $SourceRoot "build\$BuildName-io-off"
    foreach ($dir in @($autoBuild, $onBuild, $offBuild)) {
        if (Test-Path $dir) {
            Remove-Item -Recurse -Force $dir
        }
    }

    $autoConfigure =
        $Common +
        "cmake -S `"$SourceRoot`" -B `"$autoBuild`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=OFF " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_FORGE_IO=AUTO"
    $autoOutput = Invoke-NativeOutput "gate check: FORGE_ENABLE_FORGE_IO=AUTO" $autoConfigure
    Assert-OutputContains `
        -Output $autoOutput `
        -Needle "CC Forge: forge::io windows IOCP backend enabled" `
        -Label "FORGE_ENABLE_FORGE_IO=AUTO gate check"

    $onConfigure =
        $Common +
        "cmake -S `"$SourceRoot`" -B `"$onBuild`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=OFF " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_FORGE_IO=ON"
    $onOutput = Invoke-NativeOutput "gate check: FORGE_ENABLE_FORGE_IO=ON" $onConfigure
    Assert-OutputContains `
        -Output $onOutput `
        -Needle "CC Forge: forge::io windows IOCP backend enabled" `
        -Label "FORGE_ENABLE_FORGE_IO=ON gate check"

    $offConfigure =
        $Common +
        "cmake -S `"$SourceRoot`" -B `"$offBuild`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=OFF " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_FORGE_IO=OFF"
    $offOutput = Invoke-NativeOutput "gate check: FORGE_ENABLE_FORGE_IO=OFF" $offConfigure
    Assert-OutputContains `
        -Output $offOutput `
        -Needle "CC Forge: forge::io backend disabled" `
        -Label "FORGE_ENABLE_FORGE_IO=OFF gate check"

    Write-Host "[msvc] gate checks verified"
}

function Invoke-InstallPackageCheck {
    param(
        [Parameter(Mandatory = $true)][string]$Common,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$BuildName
    )

    $root = Join-Path $SourceRoot "build\$BuildName-install-package"
    $forgeBuild = Join-Path $root "forge-build"
    $prefix = Join-Path $root "prefix"
    $consumerBuild = Join-Path $root "consumer-build"
    $consumerSource = Join-Path $SourceRoot "test\install_consumer"

    if (Test-Path $root) {
        Remove-Item -Recurse -Force $root
    }

    $configure =
        $Common +
        "cmake -S `"$SourceRoot`" -B `"$forgeBuild`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=OFF " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_INSTALL=ON " +
        "-DCMAKE_INSTALL_PREFIX=`"$prefix`""
    Invoke-Native "install package: configure Forge" $configure
    Invoke-Native "install package: build Forge" ($Common + "cmake --build `"$forgeBuild`"")
    Invoke-Native "install package: install Forge" ($Common + "cmake --install `"$forgeBuild`"")

    $consumerConfigure =
        $Common +
        "cmake -S `"$consumerSource`" -B `"$consumerBuild`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DCMAKE_PREFIX_PATH=`"$prefix`""
    Invoke-Native "install package: configure consumer" $consumerConfigure
    Invoke-Native "install package: build consumer" ($Common + "cmake --build `"$consumerBuild`"")
    Invoke-Native "install package: run consumer" ($Common + "`"$consumerBuild\ccforge_install_consumer.exe`"")

    Write-Host "[msvc] install package smoke verified"
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
    Invoke-NativeOutput "compiler version" ($common + "cl 2>&1 | findstr /C:`"Version`"") | Out-Null

    if (-not $SkipGateChecks) {
        Invoke-GateChecks -Common $common -SourceRoot $sourceRoot -BuildName $BuildName
    }

    $configure =
        $common +
        "cmake -S `"$sourceRoot`" -B `"$buildDir`" -G Ninja " +
        "-DCMAKE_BUILD_TYPE=Debug " +
        "-DCMAKE_CXX_STANDARD=23 " +
        "-DFORGE_BUILD_TESTS=ON " +
        "-DFORGE_BUILD_EXAMPLES=OFF " +
        "-DFORGE_ENABLE_FORGE_IO=AUTO " +
        "-DFORGE_TEST_ENABLE_SIMD=OFF " +
        "-DFORGE_TEST_ENABLE_SUBMDSPAN=OFF " +
        "-DFORGE_TEST_ENABLE_LINALG=OFF " +
        "-DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF"
    $build = $common + "cmake --build `"$buildDir`""
    $listTests = $common + "ctest --test-dir `"$buildDir`" -N -R `"$CTestRegex`""
    $test = $common + "ctest --test-dir `"$buildDir`" -R `"$CTestRegex`" --output-on-failure"

    $configureOutput = Invoke-NativeOutput "configure" $configure
    Assert-OutputContains `
        -Output $configureOutput `
        -Needle "CC Forge: forge::io windows IOCP backend enabled" `
        -Label "main smoke IO gate"
    Assert-OutputContains `
        -Output $configureOutput `
        -Needle "CC Forge: forge::accel mock backend enabled" `
        -Label "main smoke accel gate"
    Invoke-Native "build" $build
    $testListOutput = Invoke-NativeOutput "list tests" $listTests
    $testCount = Get-CtestCount $testListOutput
    Write-Host "[msvc] ctest-count=$testCount"
    Invoke-Native "test" $test

    if (-not $SkipInstallPackageCheck) {
        Invoke-InstallPackageCheck -Common $common -SourceRoot $sourceRoot -BuildName $BuildName
    }

    $success = $true
    Write-Host "[msvc] verified"
} finally {
    if ($success -and $cloned -and (-not $Keep)) {
        Remove-Item -Recurse -Force $workRoot
    } elseif ($cloned) {
        Write-Host "[msvc] kept work root: $workRoot"
    }
}
