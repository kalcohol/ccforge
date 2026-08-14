# Shared process and path helpers for CC Forge's Windows verification scripts.

function ConvertTo-ForgeWindowsArgument {
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.Length -ne 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $result = '"'
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
            continue
        }
        if ($character -eq '"') {
            $result += (('\' * (2 * $backslashes + 1)) -join '')
            $result += '"'
            $backslashes = 0
            continue
        }
        if ($backslashes -ne 0) {
            $result += (('\' * $backslashes) -join '')
            $backslashes = 0
        }
        $result += $character
    }
    if ($backslashes -ne 0) {
        $result += (('\' * (2 * $backslashes)) -join '')
    }
    $result += '"'
    return $result
}

function ConvertFrom-ForgeNativeText {
    param([AllowEmptyString()][string]$Text)

    if (-not $Text) {
        return @()
    }
    $lines = @($Text -split "`r?`n")
    while ($lines.Count -ne 0 -and $lines[$lines.Count - 1] -eq "") {
        if ($lines.Count -eq 1) {
            return @()
        }
        $lines = @($lines[0..($lines.Count - 2)])
    }
    return $lines
}

function Invoke-ForgeNativeCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = "",
        [switch]$Quiet
    )

    if (-not $Quiet) {
        Write-Host "[msvc] $Label"
    }

    $stdout = @()
    $stderr = @()
    $process = New-Object Diagnostics.Process
    try {
        $process.StartInfo = New-Object Diagnostics.ProcessStartInfo
        $process.StartInfo.FileName = $FilePath
        $process.StartInfo.Arguments = (($Arguments | ForEach-Object {
            ConvertTo-ForgeWindowsArgument $_
        }) -join ' ')
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        if ($WorkingDirectory) {
            $process.StartInfo.WorkingDirectory = $WorkingDirectory
        }
        if (-not $process.Start()) {
            throw "Could not start native command: $FilePath"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout = @(ConvertFrom-ForgeNativeText $stdoutTask.GetAwaiter().GetResult())
        $stderr = @(ConvertFrom-ForgeNativeText $stderrTask.GetAwaiter().GetResult())
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }

    if (-not $Quiet) {
        @($stdout + $stderr) | ForEach-Object { Write-Host $_ }
    }
    return @{
        ExitCode = $exitCode
        StdOut = $stdout
        StdErr = $stderr
        Output = @($stdout + $stderr)
    }
}

function Invoke-ForgeNativeOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = ""
    )

    $params = @{
        Label = $Label
        FilePath = $FilePath
        Arguments = $Arguments
        WorkingDirectory = $WorkingDirectory
    }
    $result = Invoke-ForgeNativeCapture @params
    if ($result.ExitCode -ne 0) {
        throw "Command failed ($Label) with exit code $($result.ExitCode)"
    }
    return $result
}

function Invoke-ForgeNative {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = ""
    )

    $params = @{
        Label = $Label
        FilePath = $FilePath
        Arguments = $Arguments
        WorkingDirectory = $WorkingDirectory
    }
    $null = Invoke-ForgeNativeOutput @params
}

function Import-ForgeVcvarsEnvironment {
    param([Parameter(Mandatory = $true)][string]$VcvarsPath)

    if ($VcvarsPath.Contains("%")) {
        throw "vcvars64.bat path cannot contain a literal '%' because cmd.exe reparses CALL operands"
    }

    $batch = Join-Path ([IO.Path]::GetTempPath()) `
        ("ccforge-vcvars-" + [Guid]::NewGuid().ToString("N") + ".cmd")
    $savedPath = $env:CCFORGE_VCVARS_PATH
    try {
        $env:CCFORGE_VCVARS_PATH = $VcvarsPath
        [IO.File]::WriteAllLines($batch, @(
            "@echo off",
            "call `"%CCFORGE_VCVARS_PATH%`" >nul",
            "if errorlevel 1 exit /b %errorlevel%",
            "set"
        ), [Text.Encoding]::ASCII)
        $capture = Invoke-ForgeNativeCapture `
            -Label "initialize MSVC environment" `
            -FilePath $env:ComSpec `
            -Arguments @("/d", "/q", "/c", $batch) `
            -Quiet
    } finally {
        if ($null -eq $savedPath) {
            Remove-Item Env:\CCFORGE_VCVARS_PATH -ErrorAction SilentlyContinue
        } else {
            $env:CCFORGE_VCVARS_PATH = $savedPath
        }
        Remove-Item -LiteralPath $batch -Force -ErrorAction SilentlyContinue
    }
    if ($capture.ExitCode -ne 0) {
        throw "vcvars64.bat failed with exit code $($capture.ExitCode): $($capture.Output -join '; ')"
    }

    foreach ($line in $capture.StdOut) {
        $separator = $line.IndexOf("=")
        if ($separator -le 0) {
            continue
        }
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        if ($name -eq "CCFORGE_VCVARS_PATH") {
            continue
        }
        [Environment]::SetEnvironmentVariable($name, $value, "Process")
    }
}

function Assert-ForgeOrdinaryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a symbolic link, junction, or other reparse point: $Path"
    }
}

function Assert-ForgeTreeHasNoReparsePoints {
    param([Parameter(Mandatory = $true)][string]$Root)

    if (-not (Test-Path -LiteralPath $Root)) {
        return
    }
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($Root)
    while ($pending.Count -ne 0) {
        $directory = $pending.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Verification build tree contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Push($item.FullName)
            }
        }
    }
}

function Assert-ForgeBuildName {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or
            $Name -eq "." -or $Name -eq "..") {
        throw "BuildName must be one filename segment containing only letters, digits, '.', '_' or '-': $Name"
    }
}

function Get-ForgeBuildChild {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$ChildName
    )

    Assert-ForgeBuildName $ChildName
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $SourceRoot "build"))
    $candidate = [IO.Path]::GetFullPath((Join-Path $buildRoot $ChildName))
    $prefix = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Verification build path escaped the source build root: $candidate"
    }
    Assert-ForgeOrdinaryPath $buildRoot "Verification build root"
    Assert-ForgeOrdinaryPath $candidate "Verification build directory"
    return $candidate
}

function Reset-ForgeBuildChild {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$ChildName
    )

    $path = Get-ForgeBuildChild -SourceRoot $SourceRoot -ChildName $ChildName
    if (Test-Path -LiteralPath $path) {
        Assert-ForgeTreeHasNoReparsePoints $path
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    return $path
}

function New-ForgeVerificationWorkRoot {
    $path = Join-Path ([IO.Path]::GetTempPath()) `
        ("ccforge-win-msvc-" + [Guid]::NewGuid().ToString("N"))
    $null = New-Item -ItemType Directory -Path $path
    return $path
}

function Test-ForgeMultiConfigGenerator {
    param([Parameter(Mandatory = $true)][string]$Generator)
    return $Generator -match 'Multi-Config|Visual Studio|Xcode'
}
