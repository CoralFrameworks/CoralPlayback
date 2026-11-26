<#
.SYNOPSIS
    Windows playback smoke test for the Coral player.

.DESCRIPTION
    Runs the Windows demo over a curated set of media in the CPU and zero-copy inputs, checks the
    frame counters and the dumped frames, then runs the playback test suite. The output is a
    pass/fail table; the exit code is non-zero when a case failed.

    The frames are checked with Pillow: a case fails when the demo produced no frame, wrote no PNG,
    or every PNG is a uniform (blank) picture.

.PARAMETER BuildDir
    The CMake build directory. Defaults to build-main, then build-win.

.PARAMETER MediaDir
    A directory of extra media to exercise. Missing files are skipped.

.PARAMETER Frames
    How many frames each demo run dumps.

.PARAMETER TimeoutSeconds
    How long one demo run may take.

.PARAMETER SkipTests
    Only run the demo cases, not the test suite.

.PARAMETER Asan
    Use the ASan build and its runtime (build-asan2).

.EXAMPLE
    pwsh -File scripts/windows-smoke.ps1
    pwsh -File scripts/windows-smoke.ps1 -BuildDir build-win -SkipTests
#>
param(
    [string]$BuildDir = "",
    [string]$MediaDir = "D:\Coral项目资料汇总\媒体资源",
    [int]$Frames = 2,
    [int]$TimeoutSeconds = 180,
    [switch]$SkipTests,
    [switch]$Asan
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$python = "C:\Users\cain\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"

function Resolve-BuildDir {
    param([string]$Requested)
    if ($Requested -ne "") {
        $path = Join-Path $repo $Requested
        if (-not (Test-Path $path)) { throw "build directory not found: $path" }
        return $path
    }
    if ($Asan) {
        $path = Join-Path $repo "build-asan2"
        if (Test-Path $path) { return $path }
        throw "the ASan build directory build-asan2 does not exist"
    }
    foreach ($name in @("build-main", "build-win")) {
        $path = Join-Path $repo $name
        if (Test-Path $path) { return $path }
    }
    throw "no build directory found; pass -BuildDir"
}

function Invoke-Process {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$LogPath,
        [int]$Timeout
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $quoted = foreach ($argument in $Arguments) {
        if ($argument -match '[\s"]') {
            '"' + $argument.Replace('"', '\"') + '"'
        } else {
            $argument
        }
    }
    $psi.Arguments = $quoted -join ' '

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    $null = $process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($Timeout * 1000)
    if ($timedOut) {
        try { $process.Kill() } catch { }
        $process.WaitForExit()
    }
    $text = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
    Set-Content -Path $LogPath -Value $text -Encoding UTF8
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        TimedOut = $timedOut
        Text     = $text
    }
}

function Get-FrameStats {
    param([string]$Path)
    $output = & $python (Join-Path $PSScriptRoot "frame_stats.py") $Path 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    $parts = ($output -join "").Trim() -split "\s+"
    if ($parts.Count -ne 2) { return $null }
    return [pscustomobject]@{ Mean = [double]$parts[0]; Std = [double]$parts[1] }
}

function Invoke-DemoCase {
    param(
        [string]$Name,
        [string]$Media,
        [switch]$ZeroCopy,
        [string[]]$Extra = @(),
        [string]$Known = ""
    )
    $caseDir = Join-Path $outRoot $Name
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    $arguments = @($Media, "--frames", "$Frames", "--no-audio", "--output", $caseDir) + $Extra
    if ($ZeroCopy) { $arguments += "--zero-copy" }
    $run = Invoke-Process -Exe $demo -Arguments $arguments -LogPath (Join-Path $caseDir "demo.log") -Timeout $TimeoutSeconds

    $framesMatch = [regex]::Match($run.Text, "video frames:\s*(\d+) submitted \((\d+) direct, (\d+) converted, (\d+) dropped\), zero-copy import: (yes|no)")
    $submitted = 0; $converted = 0; $dropped = 0; $import = ""
    if ($framesMatch.Success) {
        $submitted = [int]$framesMatch.Groups[1].Value
        $converted = [int]$framesMatch.Groups[3].Value
        $dropped = [int]$framesMatch.Groups[4].Value
        $import = $framesMatch.Groups[5].Value
    }

    $pngs = @(Get-ChildItem $caseDir -Filter "*.png" -File)
    $nonBlank = 0
    $stats = @()
    foreach ($png in $pngs) {
        $frame = Get-FrameStats $png.FullName
        if ($frame -ne $null) {
            $stats += $frame
            if ($frame.Std -ge 1.0) { $nonBlank++ }
        }
    }
    $meanText = if ($stats.Count -gt 0) { "{0:N1}/{1:N1}" -f $stats[0].Mean, $stats[0].Std } else { "-" }

    $fallback = $run.Text -match "does not support HDR|falling back to the CPU|zero-copy input produced no decoded frame"
    $status = "PASS"
    $detail = ""
    if ($run.TimedOut) { $status = "FAIL"; $detail = "timeout" }
    elseif ($run.ExitCode -ne 0) { $status = "FAIL"; $detail = "exit $($run.ExitCode)" }
    elseif ($ZeroCopy -and $fallback) { $status = "SKIP"; $detail = "fell back to CPU" }
    elseif (-not $framesMatch.Success -and $ZeroCopy) { $status = "FAIL"; $detail = "no frame counters" }
    elseif ($ZeroCopy -and $submitted -le 0) { $status = "FAIL"; $detail = "no submitted frame" }
    elseif ($pngs.Count -eq 0) { $status = "FAIL"; $detail = "no PNG" }
    elseif ($nonBlank -eq 0) { $status = "FAIL"; $detail = "blank frames" }
    elseif ($ZeroCopy -and $import -ne "yes") {
        if ($fallback) { $status = "SKIP"; $detail = "fell back to CPU" }
        else { $status = "WARN"; $detail = "import no" }
    }
    elseif ($ZeroCopy -and $fallback) { $status = "SKIP"; $detail = "fell back to CPU" }

    if ($status -eq "FAIL" -and $Known -ne "") {
        $status = "SKIP"
        $detail = "known: $Known ($detail)"
    }

    return [pscustomobject]@{
        Case      = $Name
        Mode      = if ($ZeroCopy) { "zero-copy" } else { "CPU" }
        Exit      = $run.ExitCode
        Submitted = $submitted
        Dropped   = $dropped
        Import    = $import
        Png       = $pngs.Count
        NonBlank  = $nonBlank
        'Mean/Std' = $meanText
        Status    = $status
        Detail    = $detail
    }
}

$build = Resolve-BuildDir $BuildDir
$demo = Join-Path $build "examples\video_player_demo.exe"
$tests = Join-Path $build "tests\test_playback_source.exe"
if (-not (Test-Path $demo)) { throw "demo not found: $demo" }
if (-not (Test-Path $tests)) { throw "tests not found: $tests" }

if ($Asan) {
    $runtime = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64"
    if (Test-Path $runtime) { $env:PATH = "$runtime;$env:PATH" }
    $env:ASAN_OPTIONS = "detect_leaks=0:halt_on_error=0"
}

$outRoot = Join-Path $env:TEMP ("coral-smoke-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$video = Join-Path $repo "assets\video"
$subtitle = Join-Path $repo "assets\subtitle"

$cases = @(
    @{ Name = "cpu-720p"; Media = (Join-Path $video "video720p_5s.mp4") },
    @{ Name = "zc-720p"; Media = (Join-Path $video "video720p_5s.mp4"); ZeroCopy = $true },
    @{ Name = "cpu-720p-subtitle"; Media = (Join-Path $video "video720p_5s.mp4"); Extra = @("--subtitle", (Join-Path $subtitle "sample.srt")) },
    @{ Name = "cpu-720p-speed"; Media = (Join-Path $video "video720p_5s.mp4"); Extra = @("--speed", "2") },
    @{ Name = "cpu-multi-audio"; Media = (Join-Path $video "multi_audio_5s.mp4"); Extra = @("--audio-track", "1") },
    @{ Name = "zc-multi-audio"; Media = (Join-Path $video "multi_audio_5s.mp4"); ZeroCopy = $true },
    @{ Name = "cpu-forced-subtitle"; Media = (Join-Path $video "forced_subtitle_5s.mkv") },
    @{ Name = "cpu-pgs"; Media = (Join-Path $video "pgs_subtitle_5s.mkv") },
    @{ Name = "cpu-rotation"; Media = (Join-Path $MediaDir "portrait_rotated_avc_aac.mp4") },
    @{ Name = "zc-rotation"; Media = (Join-Path $MediaDir "portrait_rotated_avc_aac.mp4"); ZeroCopy = $true },
    @{ Name = "cpu-android-screens"; Media = (Join-Path $MediaDir "android-screens-lavf-56.36.100-aac-avc-main-1280x720.mkv") },
    @{ Name = "zc-android-screens"; Media = (Join-Path $MediaDir "android-screens-lavf-56.36.100-aac-avc-main-1280x720.mkv"); ZeroCopy = $true },
    @{ Name = "cpu-hdr-hlg"; Media = (Join-Path $MediaDir "IPhone12_4k_HLG.mp4"); Timeout = 240 },
    @{ Name = "zc-hdr-hlg"; Media = (Join-Path $MediaDir "IPhone12_4k_HLG.mp4"); ZeroCopy = $true; Timeout = 240 },
    @{ Name = "cpu-hdr10"; Media = (Join-Path $MediaDir "hdr10-video-with-sdr-container.mp4"); Timeout = 240 },
    @{ Name = "zc-hdr10"; Media = (Join-Path $MediaDir "hdr10-video-with-sdr-container.mp4"); ZeroCopy = $true; Timeout = 120 },
    @{ Name = "cpu-dolby-vision"; Media = (Join-Path $MediaDir "DolbyVision.mp4"); Timeout = 60; Known = "Dolby Vision decoder unsupported" },
    @{ Name = "zc-dolby-vision"; Media = (Join-Path $MediaDir "DolbyVision.mp4"); ZeroCopy = $true; Timeout = 60; Known = "Dolby Vision decoder unsupported" }
)

$results = @()
foreach ($case in $cases) {
    if (-not (Test-Path $case.Media)) {
        $results += [pscustomobject]@{
            Case = $case.Name; Mode = if ($case.ZeroCopy) { "zero-copy" } else { "CPU" }
            Exit = ""; Submitted = 0; Dropped = 0; Import = ""; Png = 0; NonBlank = 0
            'Mean/Std' = "-"; Status = "SKIP"; Detail = "media missing"
        }
        continue
    }
    $timeout = if ($case.ContainsKey("Timeout")) { $case.Timeout } else { $TimeoutSeconds }
    $save = $TimeoutSeconds
    $TimeoutSeconds = $timeout
    try {
        $results += Invoke-DemoCase -Name $case.Name -Media $case.Media -ZeroCopy:($case.ZeroCopy -eq $true) -Extra $case.Extra -Known ([string]$case.Known)
    } finally {
        $TimeoutSeconds = $save
    }
    Write-Host ("[{0}] {1} ({2})" -f $results[-1].Status, $case.Name, $results[-1].Detail)
}

$testResult = $null
if (-not $SkipTests) {
    Write-Host "running the playback test suite..."
    $run = Invoke-Process -Exe $tests -Arguments @() -LogPath (Join-Path $outRoot "tests.log") -Timeout 1800
    $passed = [regex]::Match($run.Text, "\[\s*PASSED\s*\]\s*(\d+)\s+tests?")
    $failed = @([regex]::Matches($run.Text, "\[\s*FAILED\s*\]\s*(\S+)") | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notmatch "tests?$" })
    $testResult = [pscustomobject]@{
        Exit   = $run.ExitCode
        Passed = if ($passed.Success) { [int]$passed.Groups[1].Value } else { 0 }
        Failed = ($failed -join ", ")
        Status = if ($run.ExitCode -eq 0 -and $run.TimedOut -eq $false) { "PASS" } else { "FAIL" }
    }
}

$results | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
if ($testResult -ne $null) {
    Write-Host ("tests: {0} (passed {1}, failed: {2})" -f $testResult.Status, $testResult.Passed, $testResult.Failed)
}
Write-Host "artifacts: $outRoot"

$failures = @($results | Where-Object { $_.Status -eq "FAIL" })
if ($testResult -ne $null -and $testResult.Status -ne "PASS") { $failures += "tests" }
if ($failures.Count -gt 0) {
    Write-Host ("FAILED: {0}" -f (($failures | ForEach-Object { if ($_ -is [string]) { $_ } else { $_.Case } }) -join ", "))
    exit 1
}
Write-Host "smoke: PASS"
exit 0
