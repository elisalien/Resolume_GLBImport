# ---------------------------------------------------------------------------
#  GLB Model Source - installation pour Resolume Arena (Windows 64 bits)
#
#  Ce script :
#    1. verifie que le runtime Visual C++ dont le plugin depend est present,
#       et l'installe sinon (telechargement depuis Microsoft) ;
#    2. copie GlbSource.dll dans le dossier Extra Effects de Resolume.
#
#  Lance-le via installer.bat (double-clic).
# ---------------------------------------------------------------------------

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Say-Step($t)  { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }
function Say-Ok($t)    { Write-Host "   [ok] $t" -ForegroundColor Green }
function Say-Warn($t)  { Write-Host "   [!]  $t" -ForegroundColor Yellow }
function Say-Fail($t)  { Write-Host "   [X]  $t" -ForegroundColor Red }

Write-Host "GLB Model Source - installation pour Resolume Arena" -ForegroundColor White

# --------------------------------------------------------------- 1. plateforme
Say-Step "Verification de la plateforme"
if ( -not [Environment]::Is64BitOperatingSystem ) {
    Say-Fail "Windows 32 bits detecte. Ce plugin est uniquement 64 bits."
    exit 1
}
Say-Ok "Windows 64 bits"

$dll = Join-Path $here 'GlbSource.dll'
if ( -not (Test-Path $dll) ) {
    Say-Fail "GlbSource.dll est introuvable a cote de ce script."
    Say-Fail "Dezippe l'archive entiere avant de lancer l'installation."
    exit 1
}

# ------------------------------------------------------- 2. runtime Visual C++
# Le plugin importe MSVCP140.dll, VCRUNTIME140.dll et VCRUNTIME140_1.dll.
# Sans eux Resolume ignore le plugin en silence, sans message d'erreur.
Say-Step "Verification du runtime Visual C++ 2015-2022 (x64)"
$sys = Join-Path $env:SystemRoot 'System32'
$needed = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
$missing = @( $needed | Where-Object { -not (Test-Path (Join-Path $sys $_)) } )

if ( $missing.Count -eq 0 ) {
    Say-Ok "Deja present"
} else {
    Say-Warn ("Manquant : " + ($missing -join ', '))
    $redist = Join-Path $env:TEMP 'vc_redist.x64.exe'
    try {
        Write-Host "   Telechargement depuis Microsoft..."
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' `
                          -OutFile $redist -UseBasicParsing
        Say-Ok "Telecharge"
    } catch {
        Say-Fail "Telechargement impossible : $($_.Exception.Message)"
        Say-Fail "Installe manuellement 'Visual C++ Redistributable x64' depuis"
        Say-Fail "https://aka.ms/vs/17/release/vc_redist.x64.exe puis relance ce script."
        exit 1
    }

    Write-Host "   Installation (une fenetre d'elevation va s'ouvrir)..."
    try {
        $p = Start-Process -FilePath $redist -ArgumentList '/install','/passive','/norestart' `
                           -Verb RunAs -Wait -PassThru
    } catch {
        Say-Fail "Elevation refusee ou annulee. Le runtime n'a pas ete installe."
        Say-Fail "Relance ce script et accepte l'invite, ou installe manuellement"
        Say-Fail "https://aka.ms/vs/17/release/vc_redist.x64.exe"
        exit 1
    }
    # 0 = installe, 1638 = deja une version plus recente, 3010 = ok mais redemarrage demande
    if ( $p.ExitCode -in @(0, 1638, 3010) ) {
        Say-Ok "Runtime installe (code $($p.ExitCode))"
        if ( $p.ExitCode -eq 3010 ) { Say-Warn "Un redemarrage de Windows est conseille." }
    } else {
        Say-Fail "L'installation du runtime a echoue (code $($p.ExitCode))."
        exit 1
    }
    Remove-Item $redist -ErrorAction SilentlyContinue
}

# ------------------------------------------------------ 3. dossier de Resolume
# GetFolderPath suit la redirection OneDrive, contrairement a %USERPROFILE%\Documents.
Say-Step "Recherche du dossier de plugins Resolume"
$docs = [Environment]::GetFolderPath('MyDocuments')
if ( [string]::IsNullOrWhiteSpace($docs) ) { $docs = Join-Path $env:USERPROFILE 'Documents' }
$target = Join-Path $docs 'Resolume Arena\Extra Effects'

if ( -not (Test-Path $target) ) {
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Say-Warn "Dossier cree : $target"
    Say-Warn "(Si Resolume Avenue est utilise a la place d'Arena, copie le .dll dans"
    Say-Warn " Documents\Resolume Avenue\Extra Effects.)"
} else {
    Say-Ok $target
}

# -------------------------------------------------- 4. Resolume doit etre ferme
Say-Step "Verification que Resolume est ferme"
$running = @( Get-Process -Name 'Arena','Avenue' -ErrorAction SilentlyContinue )
if ( $running.Count -gt 0 ) {
    Say-Fail "Resolume tourne. Ferme-le puis relance ce script :"
    Say-Fail "un plugin deja charge est verrouille, et le scan des plugins"
    Say-Fail "ne se fait qu'au demarrage de toute facon."
    exit 1
}
Say-Ok "Ferme"

# --------------------------------------------------------------- 5. copie
Say-Step "Installation du plugin"
try {
    Copy-Item -Path $dll -Destination $target -Force
    $installed = Join-Path $target 'GlbSource.dll'
    $size = (Get-Item $installed).Length
    Say-Ok "GlbSource.dll copie ($size octets)"
} catch {
    Say-Fail "Copie impossible : $($_.Exception.Message)"
    exit 1
}

Write-Host ""
Write-Host "Termine." -ForegroundColor Green
Write-Host "Lance Resolume, puis onglet Sources du panneau Effects -> GLB Model Source."
Write-Host "Glisse-la dans un clip, puis parametre Model File -> choisis ton .glb."
Write-Host ""
Write-Host "Si elle n'apparait pas : Ctrl+Shift+L ouvre la console de Resolume,"
Write-Host "qui liste les plugins refuses et la raison du refus."
Write-Host ""
