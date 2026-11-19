$ErrorActionPreference = "Stop"

function Send-MumpfEvent {
    param([hashtable]$Payload)
    $json = ($Payload | ConvertTo-Json -Compress) + "`n"
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", "mumpf-events", [System.IO.Pipes.PipeDirection]::Out)
    try {
        $pipe.Connect(1500)
        $pipe.Write($bytes, 0, $bytes.Length)
        $pipe.Flush()
    } finally {
        $pipe.Dispose()
    }
}

Write-Host "1/6: Build startet..."
Send-MumpfEvent @{ source = "kr-ide"; type = "build_started"; target = "WINDOWS" }
Start-Sleep -Seconds 2

Write-Host "2/6: Erste Warnung..."
Send-MumpfEvent @{ source = "kr-ide"; type = "diagnostic"; severity = "warning"; warnings = 1; file = "main.kr"; line = 42; message = "Ungenutzte Variable 'tmp'" }
Start-Sleep -Milliseconds 1600

Write-Host "3/6: Zweite Warnung..."
Send-MumpfEvent @{ source = "kr-ide"; type = "diagnostic"; severity = "warning"; warnings = 2; file = "compute/fft.kr"; line = 19; message = "Implizite Konvertierung f64 -> f32" }
Start-Sleep -Milliseconds 1800

Write-Host "4/6: Build bricht mit Fehler ab (jetzt auf Mumpf klicken!)..."
Send-MumpfEvent @{
    source = "kr-ide"
    type = "build_finished"
    success = $false
    warnings = 2
    errors = 1
    severity = "error"
    file = "compute/fft.kr"
    line = 88
    message = "C-ABI Typkonflikt: 'std::string' darf nicht ueber Rust/C++-Grenze uebergeben werden."
}
Start-Sleep -Seconds 7

Write-Host "5/6: Zweiter Build startet..."
Send-MumpfEvent @{ source = "kr-ide"; type = "build_started"; target = "WINDOWS" }
Start-Sleep -Seconds 2

Write-Host "6/6: Zweiter Build erfolgreich (0 Warnungen)!"
Send-MumpfEvent @{ source = "kr-ide"; type = "build_finished"; success = $true; warnings = 0; errors = 0 }
Write-Host "Demo abgeschlossen." -ForegroundColor Green
