$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $RepoRoot "scripts\lib\WindowsVerification.ps1")

$scratch = Join-Path ([IO.Path]::GetTempPath()) `
    ("ccforge-windows-helper-test-" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $scratch
$workRoots = @()

try {
    $source = @'
using System;
using System.Text;

public static class ArgEcho {
    public static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--json") {
            Console.Out.WriteLine("{\"tests\":[{},{}]}");
            Console.Error.WriteLine("diagnostic on stderr");
            return 0;
        }
        foreach (var arg in args) {
            Console.Out.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(arg)));
        }
        return 0;
    }
}
'@
    $echo = Join-Path $scratch "ArgEcho.exe"
    Add-Type -TypeDefinition $source -OutputAssembly $echo `
        -OutputType ConsoleApplication

    $sentinels = @(
        'space value',
        '%PATH%',
        '^',
        '`',
        '$()',
        'double"quote',
        "single'quote",
        'trailing\',
        'space trailing\',
        'quoted"trailing\'
    )
    $capture = Invoke-ForgeNativeCapture `
        -Label "argv round trip" -FilePath $echo -Arguments $sentinels -Quiet
    if ($capture.ExitCode -ne 0 -or $capture.StdErr.Count -ne 0) {
        throw "argument echo failed"
    }
    $actual = @($capture.StdOut | ForEach-Object {
        [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
    })
    if (($actual.Count -ne $sentinels.Count) -or
            (Compare-Object $sentinels $actual -SyncWindow 0)) {
        throw "native argv round trip changed an argument"
    }

    $jsonCapture = Invoke-ForgeNativeCapture `
        -Label "stdout/stderr separation" -FilePath $echo `
        -Arguments @("--json") -Quiet
    $json = ($jsonCapture.StdOut -join "`n") | ConvertFrom-Json
    if (@($json.tests).Count -ne 2 -or
            ($jsonCapture.StdErr -join "`n") -notmatch "diagnostic") {
        throw "native stdout and stderr were not captured independently"
    }

    $fakeVcvars = Join-Path $scratch "fake-vcvars.cmd"
    [IO.File]::WriteAllLines($fakeVcvars, @(
        "@echo off",
        "set CCFORGE_IMPORT_PROBE=visible",
        "exit /b 0"
    ), [Text.Encoding]::ASCII)
    Remove-Item Env:\CCFORGE_VCVARS_PATH -ErrorAction SilentlyContinue
    Import-ForgeVcvarsEnvironment $fakeVcvars
    if ($env:CCFORGE_IMPORT_PROBE -ne "visible" -or
            $null -ne $env:CCFORGE_VCVARS_PATH) {
        throw "vcvars import leaked its transport variable or lost imported state"
    }
    Remove-Item Env:\CCFORGE_IMPORT_PROBE -ErrorAction SilentlyContinue

    $percentRejected = $false
    try {
        Import-ForgeVcvarsEnvironment (Join-Path $scratch "%PATH%\vcvars64.bat")
    } catch {
        $percentRejected = $true
    }
    if (-not $percentRejected) {
        throw "vcvars import accepted a path that cmd.exe would reinterpret"
    }

    $fakeSource = Join-Path $scratch "source"
    $null = New-Item -ItemType Directory -Path (Join-Path $fakeSource "build") `
        -Force
    $victim = Join-Path $scratch "victim"
    $null = New-Item -ItemType Directory -Path $victim
    $sentinel = Join-Path $victim "sentinel"
    Set-Content -LiteralPath $sentinel -Value "keep"

    foreach ($invalid in @("..", "..\..\victim", "C:\victim", "nested/name", "a&b")) {
        $rejected = $false
        try {
            $null = Reset-ForgeBuildChild $fakeSource $invalid
        } catch {
            $rejected = $true
        }
        if (-not $rejected) {
            throw "invalid build name was accepted: $invalid"
        }
    }
    if (-not (Test-Path -LiteralPath $sentinel)) {
        throw "invalid build name removed an unrelated path"
    }

    $junctionSource = Join-Path $scratch "junction-source"
    $junctionTarget = Join-Path $scratch "junction-target"
    $null = New-Item -ItemType Directory -Path $junctionSource
    $junctionChild = Join-Path $junctionTarget "msvc-18-smoke"
    $null = New-Item -ItemType Directory -Path $junctionChild -Force
    $junctionSentinel = Join-Path $junctionChild "sentinel"
    Set-Content -LiteralPath $junctionSentinel -Value "keep"
    $null = New-Item -ItemType Junction `
        -Path (Join-Path $junctionSource "build") -Target $junctionTarget
    $junctionRejected = $false
    try {
        $null = Reset-ForgeBuildChild $junctionSource "msvc-18-smoke"
    } catch {
        $junctionRejected = $true
    }
    if (-not $junctionRejected -or
            -not (Test-Path -LiteralPath $junctionSentinel)) {
        throw "build-root junction bypassed deletion containment"
    }

    $validBuild = Reset-ForgeBuildChild $fakeSource "msvc-18.smoke_1"
    $expectedBuild = Join-Path (Join-Path $fakeSource "build") "msvc-18.smoke_1"
    if ($validBuild -ne [IO.Path]::GetFullPath($expectedBuild)) {
        throw "valid build path was not canonicalized as expected"
    }
    $null = New-Item -ItemType Directory -Path $validBuild -Force
    $null = New-Item -ItemType Junction `
        -Path (Join-Path $validBuild "nested-junction") -Target $junctionTarget
    $nestedRejected = $false
    try {
        $null = Reset-ForgeBuildChild $fakeSource "msvc-18.smoke_1"
    } catch {
        $nestedRejected = $true
    }
    if (-not $nestedRejected -or
            -not (Test-Path -LiteralPath $junctionSentinel)) {
        throw "nested junction bypassed recursive deletion containment"
    }

    $workRoots += New-ForgeVerificationWorkRoot
    $workRoots += New-ForgeVerificationWorkRoot
    if ($workRoots[0] -eq $workRoots[1]) {
        throw "verification work roots were not unique"
    }
    if (-not (Test-ForgeMultiConfigGenerator "Ninja Multi-Config") -or
            (Test-ForgeMultiConfigGenerator "Ninja")) {
        throw "multi-config generator classification is wrong"
    }
} finally {
    foreach ($root in $workRoots) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
