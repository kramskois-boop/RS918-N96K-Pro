param(
    [string]$User,
    [string]$Repo = "RS918-N96K-Pro",
    [ValidateSet("https", "ssh")][string]$Protocol = "https",
    [switch]$Check,
    [switch]$Push,
    [switch]$Force
)
# github-setup.ps1 - point this checkout at your own GitHub repository and, optionally, do the first push.
# See docs/GITHUB.md. Safe defaults: without -Push nothing is written to the network; the remote
# configuration is local only. Credentials are never stored in this script or in git config.
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

$root = git rev-parse --show-toplevel 2>$null
if (-not $root) { Fail "this directory is not a git repository" }
Set-Location $root
Write-Host "repository: $root"

if (-not $User) {
    Fail "pass the GitHub account, e.g. -User your-login (see docs/GITHUB.md)"
}

$url = if ($Protocol -eq "ssh") { "git@github.com:$User/$Repo.git" } else { "https://github.com/$User/$Repo.git" }

$dirty = @(git status --porcelain)
if ($dirty.Count -gt 0) {
    Write-Host "working tree is not clean:" -ForegroundColor Yellow
    $dirty | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
    if (-not $Force) { Fail "commit or stash the changes, or pass -Force" }
}

# Keep the upstream project reachable for future reference: the imported tree still has
# origin = df8oe/UHSDR. Rename it to "upstream" (once) before adding your own origin.
$remotes = @(git remote)
if ($remotes -contains "origin") {
    $current = (git remote get-url origin).Trim()
    if ($current -match "df8oe/UHSDR") {
        git remote rename origin upstream
        Write-Host "renamed origin -> upstream (the upstream project stays available as 'upstream')"
    } elseif ($current -ne $url) {
        if (-not $Force) { Fail "origin already exists and points to $current; pass -Force to repoint it" }
        git remote set-url origin $url
        Write-Host "origin updated: $url"
    }
}
if (-not (@(git remote) -contains "origin")) {
    git remote add origin $url
    Write-Host "origin added: $url"
} elseif ((git remote get-url origin).Trim() -ne $url) {
    git remote set-url origin $url
    Write-Host "origin updated: $url"
}

Write-Host ""
Write-Host "authentication is handled by Git Credential Manager (GCM)." -ForegroundColor Cyan
Write-Host "  first login (OAuth in browser):  git credential-manager github login"
Write-Host "  first login (device code):       git credential-manager github login --no-ui"
Write-Host "  show account:                    git credential-manager github list"
Write-Host ""

# -Check / the default mode both verify that the remote is reachable and that the credentials
# in the credential store are accepted. An empty GitHub repository answers with no refs, which
# is a valid result; 401/403 means the login step is still needed.
Write-Host "checking access to $url ..."
git ls-remote origin 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Fail "cannot access $url - run 'git credential-manager github login' and try again"
}
Write-Host "access OK" -ForegroundColor Green

if (-not $Push) {
    Write-Host ""
    Write-Host "no push requested (-Push not set)." -ForegroundColor Yellow
    Write-Host "when ready: powershell -ExecutionPolicy Bypass -File tools\github-setup.ps1 -User $User -Repo $Repo -Protocol $Protocol -Push"
    exit 0
}

Write-Host ""
Write-Host "pushing main and the release tags ..."
git push -u origin main
if ($LASTEXITCODE -ne 0) { Fail "push of main failed" }
foreach ($tag in @("v1.0", "v1.1", "v1.2")) {
    if (@(git tag -l $tag).Count -gt 0) {
        git push origin $tag
        if ($LASTEXITCODE -ne 0) { Fail "push of $tag failed" }
    }
}

Write-Host ""
Write-Host "done. next steps:" -ForegroundColor Green
Write-Host "  1. open https://github.com/$User/$Repo/actions - the host-tests and docker-build jobs must pass"
Write-Host "  2. settings -> Actions -> General: allow actions and, if the release job asks for it,"
Write-Host "     set Workflow permissions to 'Read and write' (the release job uses contents: write)"
Write-Host "  3. a firmware change is released by pushing a new tag, e.g.:"
Write-Host "       git tag -a v1.3 -m 'RS918 N96K-Pro v1.3'"
Write-Host "       git push origin v1.3"
Write-Host "     the release job then builds, packs release/<tag> and publishes the GitHub Release."
