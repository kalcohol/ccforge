# verify-windows-msvc.ps1 - Windows/MSVC smoke verification for CC Forge.
#
# Run from a Windows host with Visual Studio Build Tools installed. Pass
# -Vcvars for non-standard installations, or set FORGE_WINDOWS_VC_VARS /
# FORGE_WINDOWS_VS_ROOT in the environment.

[CmdletBinding()]
param(
    [string]$SourcePath = "",
    [string]$Repo = "https://github.com/kalcohol/ccforge.git",
    [string]$Ref = "master",
    [string]$VisualStudioRoot = "",
    [string]$VsVersion = "18",
    [string]$Vcvars = "",
    [string]$BuildName = "",
    [string]$Generator = "Ninja",
    [string]$Configuration = "Debug",
    [string]$CTestRegex = "execution|unique_resource|std_target|forge",
    [switch]$Keep,
    [switch]$SkipGoogletestProvision,
    [switch]$SkipGateChecks,
    [switch]$SkipInstallPackageCheck
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\WindowsVerification.ps1")

$GoogletestCommit = "b514bdc898e2951020cbdca1304b75f5950d1f59"

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

function Get-CtestJsonCount {
    param([Parameter(Mandatory = $true)][object[]]$StdOut)

    $text = ($StdOut -join "`n")
    $json = $text | ConvertFrom-Json
    if ($null -eq $json.tests) {
        return 0
    }
    return @($json.tests).Count
}

function Assert-CtestCount {
    param(
        [Parameter(Mandatory = $true)][int]$Count,
        [Parameter(Mandatory = $true)][int]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($Count -ne $Expected) {
        throw "$Label expected $Expected tests, got $Count"
    }
}

function Assert-CtestNonZero {
    param(
        [Parameter(Mandatory = $true)][int]$Count,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($Count -le 0) {
        throw "$Label expected at least one registered test"
    }
}

function Find-ScriptRepoRoot {
    if ($SourcePath) {
        return (Resolve-Path -LiteralPath $SourcePath).Path
    }

    if ($PSScriptRoot) {
        $candidate = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
        if ((Test-Path -LiteralPath (Join-Path $candidate "CMakeLists.txt")) -and
                (Test-Path -LiteralPath (Join-Path $candidate "forge.cmake"))) {
            return $candidate
        }
    }

    return ""
}

function Resolve-Vcvars {
    if ($Vcvars) {
        return (Resolve-Path -LiteralPath $Vcvars).Path
    }

    if ($env:FORGE_WINDOWS_VC_VARS) {
        return (Resolve-Path -LiteralPath $env:FORGE_WINDOWS_VC_VARS).Path
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
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $vswhere = ""
    if (${env:ProgramFiles(x86)}) {
        $candidate = Join-Path ${env:ProgramFiles(x86)} `
            "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $candidate) {
            $vswhere = $candidate
        }
    }

    if ($vswhere) {
        $install = & $vswhere -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -latest -property installationPath
        if ($install) {
            $candidate = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    throw "vcvars64.bat not found. Pass -Vcvars or set FORGE_WINDOWS_VC_VARS / FORGE_WINDOWS_VS_ROOT."
}

function Get-ConfigurePrefix {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$GeneratorName,
        [Parameter(Mandatory = $true)][string]$Config,
        [Parameter(Mandatory = $true)][bool]$MultiConfig
    )

    $args = @("-S", $SourceRoot, "-B", $BuildRoot)
    if ($GeneratorName) {
        $args += @("-G", $GeneratorName)
    }
    if (-not $MultiConfig) {
        $args += "-DCMAKE_BUILD_TYPE=$Config"
    }
    return $args
}

function Get-BuildArguments {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$Config,
        [string]$Target = ""
    )

    $args = @("--build", $BuildRoot, "--config", $Config)
    if ($Target) {
        $args += @("--target", $Target)
    }
    return $args
}

function Get-CtestArguments {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$Config
    )
    return @("--test-dir", $BuildRoot, "-C", $Config)
}

function Get-BuiltExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Config,
        [Parameter(Mandatory = $true)][bool]$MultiConfig
    )

    $directory = $BuildRoot
    if ($MultiConfig) {
        $directory = Join-Path $BuildRoot $Config
    }
    return Join-Path $directory "$Name.exe"
}

function Invoke-GateChecks {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$GeneratorName,
        [Parameter(Mandatory = $true)][string]$Config,
        [Parameter(Mandatory = $true)][bool]$MultiConfig
    )

    $autoBuild = Reset-ForgeBuildChild $SourceRoot "$Name-io-auto"
    $onBuild = Reset-ForgeBuildChild $SourceRoot "$Name-io-on"
    $offBuild = Reset-ForgeBuildChild $SourceRoot "$Name-io-off"
    $commonOptions = @(
        "-DCMAKE_CXX_STANDARD=23",
        "-DFORGE_BUILD_TESTS=ON",
        "-DFORGE_BUILD_EXAMPLES=ON",
        "-DFORGE_TEST_ENABLE_SIMD=OFF",
        "-DFORGE_TEST_ENABLE_SUBMDSPAN=OFF",
        "-DFORGE_TEST_ENABLE_LINALG=OFF",
        "-DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF"
    )

    $autoArgs = @(Get-ConfigurePrefix $SourceRoot $autoBuild $GeneratorName $Config $MultiConfig)
    $autoArgs += $commonOptions + "-DFORGE_ENABLE_FORGE_IO=AUTO"
    $autoOutput = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=AUTO" `
        -FilePath "cmake" -Arguments $autoArgs
    Assert-OutputContains $autoOutput.Output `
        "CC Forge: forge::io windows IOCP backend enabled" `
        "FORGE_ENABLE_FORGE_IO=AUTO gate check"
    $autoCtest = @(Get-CtestArguments $autoBuild $Config)
    $autoCtest += @("--show-only=json-v1", "-R", "forge_io|example_forge_io")
    $autoTests = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=AUTO registered tests" `
        -FilePath "ctest" -Arguments $autoCtest
    Assert-CtestNonZero (Get-CtestJsonCount $autoTests.StdOut) `
        "FORGE_ENABLE_FORGE_IO=AUTO registration"
    Invoke-ForgeNative `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=AUTO build selected IO example" `
        -FilePath "cmake" `
        -Arguments (Get-BuildArguments $autoBuild $Config "forge_io_iocp")
    $autoExample = @(Get-CtestArguments $autoBuild $Config)
    $autoExample += @("-R", "^example_forge_io_iocp_smoke$", "--output-on-failure")
    Invoke-ForgeNative `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=AUTO run selected IO example" `
        -FilePath "ctest" -Arguments $autoExample

    $onArgs = @(Get-ConfigurePrefix $SourceRoot $onBuild $GeneratorName $Config $MultiConfig)
    $onArgs += $commonOptions + "-DFORGE_ENABLE_FORGE_IO=ON"
    $onOutput = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=ON" `
        -FilePath "cmake" -Arguments $onArgs
    Assert-OutputContains $onOutput.Output `
        "CC Forge: forge::io windows IOCP backend enabled" `
        "FORGE_ENABLE_FORGE_IO=ON gate check"
    $onCtest = @(Get-CtestArguments $onBuild $Config)
    $onCtest += @("--show-only=json-v1", "-R", "forge_io|example_forge_io")
    $onTests = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=ON registered tests" `
        -FilePath "ctest" -Arguments $onCtest
    Assert-CtestNonZero (Get-CtestJsonCount $onTests.StdOut) `
        "FORGE_ENABLE_FORGE_IO=ON registration"

    $offArgs = @(Get-ConfigurePrefix $SourceRoot $offBuild $GeneratorName $Config $MultiConfig)
    $offArgs += $commonOptions + "-DFORGE_ENABLE_FORGE_IO=OFF"
    $offOutput = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=OFF" `
        -FilePath "cmake" -Arguments $offArgs
    Assert-OutputContains $offOutput.Output `
        "CC Forge: forge::io backend disabled" `
        "FORGE_ENABLE_FORGE_IO=OFF gate check"
    $offCtest = @(Get-CtestArguments $offBuild $Config)
    $offCtest += @("--show-only=json-v1", "-R", "forge_io|example_forge_io")
    $offTests = Invoke-ForgeNativeOutput `
        -Label "gate check: FORGE_ENABLE_FORGE_IO=OFF registered tests" `
        -FilePath "ctest" -Arguments $offCtest
    Assert-CtestCount (Get-CtestJsonCount $offTests.StdOut) 0 `
        "FORGE_ENABLE_FORGE_IO=OFF registration"

    Write-Host "[msvc] gate checks verified"
}

function Invoke-InstallPackageCheck {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$GeneratorName,
        [Parameter(Mandatory = $true)][string]$Config,
        [Parameter(Mandatory = $true)][bool]$MultiConfig
    )

    $root = Reset-ForgeBuildChild $SourceRoot "$Name-install-package"
    $forgeBuild = Join-Path $root "forge-build"
    $prefix = Join-Path $root "prefix"
    $consumerBuild = Join-Path $root "consumer-build"
    $consumerSource = Join-Path $SourceRoot "test\install_consumer"

    $configure = @(Get-ConfigurePrefix $SourceRoot $forgeBuild $GeneratorName $Config $MultiConfig)
    $configure += @(
        "-DCMAKE_CXX_STANDARD=23",
        "-DFORGE_BUILD_TESTS=OFF",
        "-DFORGE_BUILD_EXAMPLES=OFF",
        "-DFORGE_ENABLE_INSTALL=ON",
        "-DCMAKE_INSTALL_PREFIX=$prefix"
    )
    Invoke-ForgeNative "install package: configure Forge" "cmake" $configure
    Invoke-ForgeNative "install package: build Forge" "cmake" `
        (Get-BuildArguments $forgeBuild $Config)
    Invoke-ForgeNative "install package: install Forge" "cmake" `
        @("--install", $forgeBuild, "--config", $Config)

    $consumerConfigure = @(Get-ConfigurePrefix `
        $consumerSource $consumerBuild $GeneratorName $Config $MultiConfig)
    $consumerConfigure += @(
        "-DCMAKE_CXX_STANDARD=23",
        "-DCMAKE_PREFIX_PATH=$prefix"
    )
    Invoke-ForgeNative "install package: configure consumer" "cmake" $consumerConfigure
    Invoke-ForgeNative "install package: build consumer" "cmake" `
        (Get-BuildArguments $consumerBuild $Config)
    Invoke-ForgeNative "install package: run consumer" `
        (Get-BuiltExecutable $consumerBuild "ccforge_install_consumer" $Config $MultiConfig)
    Invoke-ForgeNative "install package: run std consumer" `
        (Get-BuiltExecutable $consumerBuild "ccforge_install_std_consumer" $Config $MultiConfig)

    Write-Host "[msvc] install package smoke verified"
}

if (-not $BuildName) {
    $BuildName = "msvc-$VsVersion-smoke"
}
Assert-ForgeBuildName $BuildName
Assert-ForgeBuildName $Configuration
if ($VsVersion -notmatch '^[0-9]+([.][0-9]+)?$') {
    throw "VsVersion must be numeric: $VsVersion"
}

$sourceRoot = Find-ScriptRepoRoot
$workRoot = ""
$workRootCreated = $false
$verificationSucceeded = $false

try {
    if (-not $sourceRoot) {
        $workRoot = New-ForgeVerificationWorkRoot
        $workRootCreated = $true
        $sourceRoot = Join-Path $workRoot "src"
        Invoke-ForgeNative "clone $Repo" "git" `
            @("clone", "--no-checkout", "--", $Repo, $sourceRoot)

        $revision = "$Ref^{commit}"
        $resolvedOutput = Invoke-ForgeNativeOutput "resolve $Ref" "git" `
            @("-C", $sourceRoot, "rev-parse", "--verify", `
              "--end-of-options", $revision)
        $resolvedRef = ($resolvedOutput.StdOut -join "").Trim()
        if ($resolvedRef -notmatch '^[0-9a-fA-F]{40}$') {
            throw "Could not resolve an immutable commit for ref: $Ref"
        }
        Invoke-ForgeNative "checkout $resolvedRef" "git" `
            @("-C", $sourceRoot, "checkout", "--detach", $resolvedRef)
        Write-Host "[msvc] source-commit=$resolvedRef"
    }

    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "CMakeLists.txt"))) {
        throw "SourcePath does not look like the CC Forge repo: $sourceRoot"
    }

    $gtestDir = Join-Path $sourceRoot "3rdparty\googletest"
    $gtestCMake = Join-Path $gtestDir "CMakeLists.txt"
    if ((-not (Test-Path -LiteralPath $gtestCMake)) -and
            (-not $SkipGoogletestProvision)) {
        if ((Test-Path -LiteralPath (Join-Path $sourceRoot ".git")) -and
                (Test-Path -LiteralPath (Join-Path $sourceRoot ".gitmodules"))) {
            Invoke-ForgeNative "initialize googletest submodule" "git" `
                @("-C", $sourceRoot, "submodule", "update", "--init", `
                  "--depth", "1", "--", "3rdparty/googletest")
        } else {
            $null = New-Item -ItemType Directory -Force `
                -Path (Join-Path $sourceRoot "3rdparty")
            $null = New-Item -ItemType Directory -Path $gtestDir -Force
            Invoke-ForgeNative "initialize googletest repository" "git" `
                @("-C", $gtestDir, "init")
            Invoke-ForgeNative "configure googletest origin" "git" `
                @("-C", $gtestDir, "remote", "add", "origin", `
                  "https://github.com/google/googletest.git")
            Invoke-ForgeNative "fetch pinned googletest" "git" `
                @("-C", $gtestDir, "fetch", "--depth", "1", "origin", `
                  $GoogletestCommit)
            Invoke-ForgeNative "checkout pinned googletest" "git" `
                @("-C", $gtestDir, "checkout", "--detach", "FETCH_HEAD")
            $gtestHeadOutput = Invoke-ForgeNativeOutput `
                "verify pinned googletest" "git" `
                @("-C", $gtestDir, "rev-parse", "HEAD")
            $gtestHead = ($gtestHeadOutput.StdOut -join "").Trim()
            if ($gtestHead -ne $GoogletestCommit) {
                throw "GoogleTest checkout mismatch: expected $GoogletestCommit, got $gtestHead"
            }
        }
    }

    if (-not (Test-Path -LiteralPath $gtestCMake)) {
        throw "Missing 3rdparty\googletest. Re-run without -SkipGoogletestProvision or provide it manually."
    }

    $Vcvars = Resolve-Vcvars
    Import-ForgeVcvarsEnvironment $Vcvars
    $multiConfig = Test-ForgeMultiConfigGenerator $Generator
    $buildDir = Reset-ForgeBuildChild $sourceRoot $BuildName

    Write-Host "[msvc] source=$sourceRoot"
    Write-Host "[msvc] build=$buildDir"
    Write-Host "[msvc] vcvars=$Vcvars"
    Write-Host "[msvc] generator=$Generator config=$Configuration"

    $compilerVersion = Invoke-ForgeNativeCapture `
        -Label "compiler version" -FilePath "cl.exe" -Arguments @("/Bv")
    Assert-OutputContains $compilerVersion.Output "Microsoft" "compiler version"

    if (-not $SkipGateChecks) {
        Invoke-GateChecks $sourceRoot $BuildName $Generator $Configuration $multiConfig
    }

    $configure = @(Get-ConfigurePrefix `
        $sourceRoot $buildDir $Generator $Configuration $multiConfig)
    $configure += @(
        "-DCMAKE_CXX_STANDARD=23",
        "-DFORGE_BUILD_TESTS=ON",
        "-DFORGE_BUILD_EXAMPLES=OFF",
        "-DFORGE_ENABLE_FORGE_IO=AUTO",
        "-DFORGE_TEST_ENABLE_SIMD=OFF",
        "-DFORGE_TEST_ENABLE_SUBMDSPAN=OFF",
        "-DFORGE_TEST_ENABLE_LINALG=OFF",
        "-DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF"
    )
    $configureOutput = Invoke-ForgeNativeOutput "configure" "cmake" $configure
    Assert-OutputContains $configureOutput.Output `
        "CC Forge: forge::io windows IOCP backend enabled" "main smoke IO gate"
    Invoke-ForgeNative "build" "cmake" `
        (Get-BuildArguments $buildDir $Configuration)

    $listTests = @(Get-CtestArguments $buildDir $Configuration)
    $listTests += @("-N", "-R", $CTestRegex)
    $testListOutput = Invoke-ForgeNativeOutput "list tests" "ctest" $listTests
    $testCount = Get-CtestCount $testListOutput.StdOut
    Write-Host "[msvc] ctest-count=$testCount"
    Assert-CtestNonZero $testCount "main smoke CTest regex '$CTestRegex'"

    $test = @(Get-CtestArguments $buildDir $Configuration)
    $test += @("-R", $CTestRegex, "--output-on-failure")
    Invoke-ForgeNative "test" "ctest" $test

    if (-not $SkipInstallPackageCheck) {
        Invoke-InstallPackageCheck `
            $sourceRoot $BuildName $Generator $Configuration $multiConfig
    }

    Write-Host "[msvc] verified"
    $verificationSucceeded = $true
} finally {
    if ($workRootCreated -and (-not $Keep)) {
        try {
            Remove-Item -LiteralPath $workRoot -Recurse -Force `
                -ErrorAction Stop
        } catch {
            if ($verificationSucceeded) {
                throw
            }
            Write-Warning "Could not remove failed Windows verification root: $workRoot"
        }
    } elseif ($workRootCreated) {
        Write-Host "[msvc] kept work root: $workRoot"
    }
}
