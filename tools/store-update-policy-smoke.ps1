param(
    [Parameter(Mandatory = $true)]
    [string]$Exe
)

$ErrorActionPreference = 'Stop'
$resolvedExe = (Resolve-Path $Exe).Path
$bytes = [IO.File]::ReadAllBytes($resolvedExe)

function Assert-BytesAbsent([string]$Text, [Text.Encoding]$Encoding) {
    $needle = $Encoding.GetBytes($Text)
    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            throw "Store binary contains forbidden self-update marker: $Text"
        }
    }
}

Assert-BytesAbsent '/repos/everettjf/liney-win/releases/latest' ([Text.Encoding]::ASCII)
Assert-BytesAbsent 'update-self-test' ([Text.Encoding]::Unicode)
Assert-BytesAbsent 'Check for updates' ([Text.Encoding]::Unicode)

Write-Host "Store update policy verified: $resolvedExe"
