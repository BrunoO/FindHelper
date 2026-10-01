<#
.SYNOPSIS
    Recursively lists all files and directories from a given root directory
    and saves the list to a specified output file.

.DESCRIPTION
    This script is the Windows/PowerShell equivalent of the create_file_list.sh script.
    It handles permission errors silently and appends a trailing backslash to directory paths
    to ensure compatibility with the C++ application's file-based indexing.

.PARAMETER RootDirectory
    The starting directory for the recursive scan.

.PARAMETER OutputFile
    The path to the file where the list of paths will be saved.

.EXAMPLE
    .\\create_file_list.ps1 -RootDirectory "C:\\Source\\MyProject" -OutputFile "C:\\Output\\file_list.txt"
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$RootDirectory,

    [Parameter(Mandatory=$true)]
    [string]$OutputFile
)

# Check if the root directory exists
if (-not (Test-Path -Path $RootDirectory -PathType Container)) {
    Write-Error "Error: Directory '$RootDirectory' not found."
    exit 1
}

Write-Host "Scanning directory: $RootDirectory"

# Get all items recursively, ignoring errors, and process them
Get-ChildItem -Path $RootDirectory -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object {
    $path = $_.FullName
    if ($_.PSIsContainer) {
        # Append a trailing backslash for directories
        "$path\\"
    } else {
        $path
    }
} | Set-Content -Path $OutputFile -Encoding UTF8

Write-Host "File list saved to: $OutputFile"
