param([Parameter(Mandatory=$true)][string]$PythonExecutable)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'tools/verification-harness.ps1')
$fixture=Join-Path $project ('build/evidence/visual wrapper fixtures '+[Guid]::NewGuid().ToString('N'))
$mirror=Join-Path $fixture 'project'
$mirrorTools=Join-Path $mirror 'tools'
$mirrorTests=Join-Path $mirror 'tests'
New-Item -ItemType Directory -Path $mirrorTools,$mirrorTests -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'tools/run-visual-verification.ps1') -Destination $mirrorTools
Copy-Item -LiteralPath (Join-Path $project 'tools/python-runtime.ps1') -Destination $mirrorTools

# Only GPU capture is replaced. Suite selection, preflight, the native Python
# checker, stream logging, exit status and manifest completion are production code.
$realHarness="'"+(Join-Path $project 'tools/verification-harness.ps1').Replace("'","''")+"'"
$captureHarness=@'
. __REAL_HARNESS__
function Invoke-VaporCruiseCaptureMatrix {
    param($Executable,$OutputDirectory,$Frames,$ManifestName)
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Add-Type -AssemblyName System.Drawing
    $bitmap=New-Object Drawing.Bitmap 1,1
    try { $bitmap.Save((Join-Path $OutputDirectory 'fixture.png'),[Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
'@
Set-Content -LiteralPath (Join-Path $mirrorTools 'verification-harness.ps1') -Value $captureHarness.Replace('__REAL_HARNESS__',$realHarness)
Set-Content -LiteralPath (Join-Path $mirrorTests 'checker fixture.py') -Value @'
import sys
# Fill both redirected pipes beyond their normal capacity to catch serial reads.
print("STDOUT_SENTINEL")
print("stdout filler\n" * 12000)
sys.stderr.write("STDERR_SENTINEL\n")
sys.stderr.write("stderr filler\n" * 12000)
sys.exit(int(sys.argv[-1]))
'@
$exe=Join-Path $mirror 'unused.exe'
Set-Content -LiteralPath $exe -Value 'Capture is replaced; this executable is never launched.'
$shells=@(Get-Command powershell,pwsh -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Source -Unique)
if (-not $shells.Count) { throw 'No PowerShell executable available for the wrapper regression.' }
foreach ($shell in $shells) {
    foreach ($expectedExit in @(0,7)) {
        $catalog=[ordered]@{schema_version=1;profiles=@{fixture=@{frames=@(@{Name='fixture';Width=1;Height=1});manifest_name='captures.json'}};
            suites=@(@{id='native-checker';classification='current';capture_profile='fixture';baseline_required=$false;
                required_files=@('fixture.png');script='tests/checker fixture.py';arguments=@([string]$expectedExit)})}
        $catalog | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $mirrorTests 'visual-suite-manifest.json') -Encoding utf8
        $shellName=[IO.Path]::GetFileNameWithoutExtension($shell)
        $output=Join-Path $fixture ($shellName+'-exit-'+$expectedExit)
        $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $mirrorTools 'run-visual-verification.ps1'),
            '-Executable',$exe,'-OutputDirectory',$output,'-PythonExecutable',$PythonExecutable)
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=$shell
        $info.Arguments=($arguments | ForEach-Object { ConvertTo-VaporCruiseArgument $_ }) -join ' '
        $info.UseShellExecute=$false
        $info.CreateNoWindow=$true
        $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $info.RedirectStandardOutput=$true
        $info.RedirectStandardError=$true
        # A PS5 child cannot load the PowerShell 7 modules inherited from pwsh.
        # Let each selected host initialize its own module search path.
        $info.EnvironmentVariables.Remove('PSModulePath')
        $child=[Diagnostics.Process]::Start($info)
        try {
            $stdout=$child.StandardOutput.ReadToEndAsync()
            $stderr=$child.StandardError.ReadToEndAsync()
            if (-not $child.WaitForExit(30000)) { throw "$shellName wrapper fixture timed out." }
            $diagnostic=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
            if ($expectedExit -eq 0 -and $child.ExitCode -ne 0) {
                throw "Successful Python stderr rejected by $shellName (wrapper exit $($child.ExitCode)): $($diagnostic.Substring(0,[Math]::Min(400,$diagnostic.Length)))"
            }
            if ($expectedExit -ne 0 -and $child.ExitCode -eq 0) { throw "$shellName accepted failed Python exit $expectedExit." }
            $report=Get-Content -LiteralPath (Join-Path $output 'verification.json') -Raw | ConvertFrom-Json
            if ($report.completion -ne ($expectedExit -eq 0) -or @($report.results).Count -ne 1 -or
                $report.results[0].exit_code -ne $expectedExit) { throw "$shellName lost checker status or manifest completion." }
            $log=Get-Content -LiteralPath (Join-Path $output 'native-checker.log') -Raw
            if (-not $log.Contains('STDOUT_SENTINEL') -or -not $log.Contains('STDERR_SENTINEL') -or
                -not $log.Contains('stdout filler') -or -not $log.Contains('stderr filler')) { throw "$shellName lost redirected checker output." }
        } finally {
            if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit(5000) | Out-Null }
            $child.Dispose()
        }
    }
}
Write-Output "PASS: native visual checker stdout/stderr, successful and failed exits, completion, pipe draining and spaced paths ($($shells.Count) PowerShell hosts)."
