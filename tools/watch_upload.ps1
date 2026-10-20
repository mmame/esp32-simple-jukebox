# Shows the state of a running tools/upload_folders.py every few seconds (reads its log file).
param(
    [Parameter(Mandatory = $true)][string]$Log,
    [int]$Seconds = 30
)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
while ($true) {
    Clear-Host
    $lines = @(Get-Content -LiteralPath $Log -Encoding UTF8 -ErrorAction SilentlyContinue)
    $albums = @($lines | Where-Object { $_ -like '=== album*' })
    $last = $albums | Select-Object -Last 1
    $progress = $lines | Where-Object { $_ -match '^\[\s*\d+%\]' } | Select-Object -Last 1
    $problems = @($lines | Where-Object { $_ -match 'failed|FAILED|Traceback|error' })
    Write-Host ("Upload status  " + (Get-Date -Format 'HH:mm:ss') + "   (refresh every $Seconds s, Ctrl+C closes)") -ForegroundColor Cyan
    Write-Host ""
    Write-Host ("Album : " + $last)
    Write-Host ("File  : " + $progress)
    Write-Host ""
    Write-Host ("Albums started: " + $albums.Count + " / 43      Problems: " + $problems.Count)
    $problems | Select-Object -Last 3 | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
    if ($lines -match '^=== finished') {
        Write-Host ""
        Write-Host ($lines | Where-Object { $_ -like '=== finished*' } | Select-Object -Last 1) -ForegroundColor Green
        break
    }
    Start-Sleep -Seconds $Seconds
}
Read-Host "Done - press Enter to close"
