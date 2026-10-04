function Get-CaptureEventMilliseconds($EventRecord, $Metadata) {
    if ($null -eq $EventRecord.monotonic_seconds -or $null -eq $Metadata.measurement_start_monotonic_seconds) {
        throw 'QA requires a shared absolute monotonic clock in events and capture metadata.'
    }
    return ([double]$EventRecord.monotonic_seconds - [double]$Metadata.measurement_start_monotonic_seconds) * 1000
}
