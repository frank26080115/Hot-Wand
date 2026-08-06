$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$searchDirs = @('src', 'inc', 'lib', 'test')
$ignoreDirs = @(
)
$ignoreFiles = @(
)
$extensions = @('.c', '.cpp', '.h', '.hpp')

$pathSeparators = @([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
$ignoreDirPaths = @(
    foreach ($dir in $ignoreDirs) {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $dir)).TrimEnd([char[]]$pathSeparators)
    }
)
$ignoreFilePaths = @(
    foreach ($file in $ignoreFiles) {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $file))
    }
)

function Test-IsIgnoredPath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    foreach ($ignoreFilePath in $ignoreFilePaths) {
        if ($fullPath.Equals($ignoreFilePath, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    foreach ($ignoreDirPath in $ignoreDirPaths) {
        if ($fullPath.Equals($ignoreDirPath, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }

        if ($fullPath.StartsWith($ignoreDirPath + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

$clangFormat = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue
if ($null -eq $clangFormat) {
    Write-Error 'clang-format was not found on PATH. Install LLVM/clang-format or add it to PATH.'
}

$files = @(
    foreach ($dir in $searchDirs) {
        $path = Join-Path $repoRoot $dir
        if (Test-Path -LiteralPath $path -PathType Container) {
            Get-ChildItem -LiteralPath $path -Recurse -File |
                Where-Object { ($extensions -contains $_.Extension.ToLowerInvariant()) -and -not (Test-IsIgnoredPath $_.FullName) } |
                ForEach-Object { $_.FullName }
        }
    }
)

if ($files.Count -eq 0) {
    Write-Host 'No C/C++ files found to format.'
    exit 0
}

& $clangFormat.Path -i -- $files
if ($LASTEXITCODE -ne 0) {
    throw "clang-format failed with exit code $LASTEXITCODE."
}
