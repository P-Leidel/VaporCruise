# Dot-source only. Optional verification must preflight before creating outputs.
function Resolve-VaporCruisePython {
    [CmdletBinding()]
    param([string]$PythonExecutable, [string[]]$RequiredImports = @())
    foreach ($module in $RequiredImports) {
        if ($module -notmatch '^[A-Za-z_][A-Za-z0-9_.]*$') { throw "Invalid Python import: $module" }
    }
    $configured = if ($PythonExecutable) { $PythonExecutable } else { $env:VAPORCRUISE_PYTHON }
    $candidates = if ($configured) { @($configured) } else { @('python','python3','py') }
    $lastFailure = 'No Python interpreter was found on PATH.'
    foreach ($candidate in $candidates) {
        try {
            $command = Get-Command -Name $candidate -CommandType Application -ErrorAction Stop | Select-Object -First 1
            $prefix = if ([IO.Path]::GetFileNameWithoutExtension($command.Source) -eq 'py') { @('-3') } else { @() }
            $probe = 'import sys; assert sys.version_info.major == 3; print(sys.executable)'
            $result = & $command.Source @prefix -c $probe 2>&1
            if ($LASTEXITCODE -ne 0 -or @($result).Count -ne 1 -or -not (Test-Path -LiteralPath ([string]$result) -PathType Leaf)) {
                throw "Python interpreter probe failed: $candidate"
            }
            $selected = [IO.Path]::GetFullPath([string]$result)
        } catch {
            $lastFailure = "Python selection failed for '$candidate': $_"
            if ($configured) { throw $lastFailure }
            continue
        }
        $probe = 'import importlib,sys; [importlib.import_module(m) for m in sys.argv[1:]]'
        try {
            $result = & $selected -c $probe @RequiredImports 2>&1
            if ($LASTEXITCODE -ne 0) { throw "$result" }
        } catch { throw "Python import preflight failed for '$selected' ($($RequiredImports -join ', ')): $_" }
        return $selected
    }
    throw $lastFailure
}
