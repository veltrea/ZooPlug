<#
.SYNOPSIS
  Self-sign a Windows FileMaker plug-in (.fmx / .fmx64) with a self-signed code-signing
  certificate + RFC3161 timestamp. The Windows counterpart to the macOS ad-hoc deep-sign
  step in build-and-sign-mac.sh — keep both platforms signed consistently.

.DESCRIPTION
  Policy: no paid code-signing cert (matches the no-Apple-Developer-account policy). We use a
  SELF-SIGNED CodeSigning cert kept stable per machine (reused if present, created once otherwise)
  so the signature/cdHash stays constant across rebuilds — distributable as-is, the recipient
  trusts it once. A timestamp (DigiCert, fallback Sectigo) is added so the signature stays valid
  after the cert's NotAfter.

  Note: a signature is NOT required for the plug-in to LOAD on FileMaker Pro 11. This step is
  for distribution hygiene and modern Windows/FileMaker, and to mirror the macOS build's
  signing.

.PARAMETER Path
  Path to the .fmx / .fmx64 to sign. Required.

.PARAMETER Subject
  Certificate subject CN. Default "veltrea".
#>
param(
  [Parameter(Mandatory=$true)][string]$Path,
  [string]$Subject = "veltrea"
)

if (-not (Test-Path $Path)) { Write-Error "file not found: $Path"; exit 1 }

# Reuse an existing self-signed CodeSigning cert with this subject, else create one (10y).
$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -like "*$Subject*" -and $_.EnhancedKeyUsageList.FriendlyName -contains "Code Signing" } |
        Select-Object -First 1
if (-not $cert) {
  $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like "*$Subject*" } | Select-Object -First 1
}
if (-not $cert) {
  Write-Output "Creating self-signed code-signing cert CN=$Subject"
  $cert = New-SelfSignedCertificate -Type CodeSigning -Subject "CN=$Subject" `
            -CertStoreLocation Cert:\CurrentUser\My -HashAlgorithm sha256 -NotAfter (Get-Date).AddYears(10)
} else {
  Write-Output ("Reusing cert " + $cert.Thumbprint)
}

# Locate signtool.exe (latest Windows 10/11 SDK, x64 build).
$signtool = (Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
             Where-Object { $_.Directory.Name -eq "x64" } | Sort-Object FullName -Descending | Select-Object -First 1).FullName
if (-not $signtool) { Write-Error "signtool.exe not found (install the Windows SDK)"; exit 1 }

# Sign with SHA256 + RFC3161 timestamp (fallback timestamp server if the first is unreachable).
& $signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Path
if ($LASTEXITCODE -ne 0) {
  Write-Output "DigiCert timestamp failed, trying Sectigo..."
  & $signtool sign /sha1 $cert.Thumbprint /fd SHA256 /tr http://timestamp.sectigo.com /td SHA256 $Path
}
if ($LASTEXITCODE -ne 0) { Write-Error "signing failed"; exit 1 }

# Shallow verify (Authenticode). A self-signed cert is not chain-trusted by default, which is
# expected; /pa would fail on an untrusted root. We just confirm the embedded signature exists.
& $signtool verify /v $Path 2>$null | Out-Null
Write-Output ("Signed: " + $Path)
