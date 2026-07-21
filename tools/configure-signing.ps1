[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$PfxPath,

    [ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')]
    [string]$Repository = 'everettjf/liney-win',

    [Security.SecureString]$Password
)

$ErrorActionPreference = 'Stop'

function Set-GitHubSecretFromMemory {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Repo
    )

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = 'gh'
    # Windows PowerShell 5.1 uses .NET Framework and has no ArgumentList.
    # Name is internal and Repo is constrained by ValidatePattern above.
    $start.Arguments = "secret set $Name --repo $Repo"
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true

    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.StandardInput.Write($Value)
        $process.StandardInput.Close()
        $errorText = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "Unable to set GitHub secret $Name`: $errorText"
        }
    } finally {
        $process.Dispose()
    }
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'GitHub CLI (gh) is required.'
}

$resolvedPfx = (Resolve-Path -LiteralPath $PfxPath).Path
if ([IO.Path]::GetExtension($resolvedPfx) -notin @('.pfx', '.p12')) {
    throw 'The signing certificate must be a .pfx or .p12 file.'
}
if (-not $Password) {
    $Password = Read-Host 'PFX password' -AsSecureString
}

$pfx = Get-PfxData -FilePath $resolvedPfx -Password $Password
$certificate = $pfx.EndEntityCertificates | Select-Object -First 1
if (-not $certificate) {
    throw 'The PFX does not contain an end-entity certificate.'
}
if (-not $certificate.HasPrivateKey) {
    throw 'The PFX does not contain the certificate private key.'
}
$codeSigningOid = '1.3.6.1.5.5.7.3.3'
$hasCodeSigningEku = $certificate.Extensions |
    Where-Object { $_ -is [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension] } |
    ForEach-Object { $_.EnhancedKeyUsages } |
    Where-Object { $_.Value -eq $codeSigningOid }
if (-not $hasCodeSigningEku) {
    throw 'The certificate is not valid for Code Signing (EKU 1.3.6.1.5.5.7.3.3).'
}
$now = Get-Date
if ($now -lt $certificate.NotBefore -or $now -gt $certificate.NotAfter) {
    throw "The certificate is not currently valid ($($certificate.NotBefore) - $($certificate.NotAfter))."
}

& gh auth status --hostname github.com 2>$null
if ($LASTEXITCODE -ne 0) {
    throw 'GitHub CLI is not authenticated. Run gh auth login first.'
}
& gh api "repos/$Repository" --silent
if ($LASTEXITCODE -ne 0) {
    throw "Cannot access GitHub repository $Repository."
}

$passwordText = $null
$passwordPtr = [IntPtr]::Zero
try {
    $passwordPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password)
    $passwordText = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPtr)
    $base64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($resolvedPfx))

    if ($PSCmdlet.ShouldProcess($Repository, 'Configure Authenticode signing secrets')) {
        Set-GitHubSecretFromMemory -Name 'LINEY_SIGNING_CERT_BASE64' -Value $base64 -Repo $Repository
        Set-GitHubSecretFromMemory -Name 'LINEY_SIGNING_CERT_PASSWORD' -Value $passwordText -Repo $Repository
        Write-Host "Configured Authenticode signing secrets for $Repository."
        Write-Host "Certificate: $($certificate.Subject)"
        Write-Host "Valid until: $($certificate.NotAfter.ToString('u'))"
    }
} finally {
    $passwordText = $null
    $base64 = $null
    if ($passwordPtr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPtr)
    }
}
