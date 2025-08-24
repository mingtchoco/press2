# PowerShell script to fix nul file issues
# This is a backup if nul.bat is not available

$ErrorActionPreference = "SilentlyContinue"

Write-Host "NUL File Fixer for Git Projects" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green

# Function to remove nul files
function Remove-NulFile {
    param([string]$Path)
    
    $nulPath = Join-Path $Path "nul"
    $specialPath = "\\?\$nulPath"
    
    if (Test-Path -LiteralPath $specialPath) {
        Write-Host "Found NUL file at: $Path" -ForegroundColor Yellow
        
        # Try multiple methods
        $methods = @(
            { Remove-Item -LiteralPath $specialPath -Force -ErrorAction Stop },
            { [System.IO.File]::Delete($specialPath) },
            { cmd /c "del `"$specialPath`" /f /q" }
        )
        
        foreach ($method in $methods) {
            try {
                & $method
                if (-not (Test-Path -LiteralPath $specialPath)) {
                    Write-Host "  [DELETED] Successfully removed" -ForegroundColor Green
                    return $true
                }
            } catch {
                # Try next method
            }
        }
        Write-Host "  [FAILED] Could not delete" -ForegroundColor Red
        return $false
    }
    return $false
}

# Check current directory
$currentDir = Get-Location
Remove-NulFile -Path $currentDir

# Check subdirectories
Get-ChildItem -Directory -Recurse | ForEach-Object {
    Remove-NulFile -Path $_.FullName
}

Write-Host "`nCleanup complete!" -ForegroundColor Green