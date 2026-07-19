# button.ps1 - native PowerShell entry point for egg-pals on Windows.
#
# This is a thin forwarder, not a reimplementation: it finds bash.exe
# (Git for Windows or MSYS2, whichever is installed) and hands the action
# straight to button.sh. All the actual compile/run/kill logic lives in
# one place (button.sh) instead of drifting into two copies - a sibling
# project's own Windows port (see win-butt-suc-note-m31.txt /
# windows_linux_parity.txt) hit exactly that trap: a separate
# compile_all.ps1 grew hardcoded paths and fell out of sync with the .sh
# version. Don't repeat that here.
#
# Usage: .\button.ps1 <action>   (same actions as button.sh: compile, run,
# check, demo, icons, kill, help)

param(
    [Parameter(Position = 0)]
    [string]$Action = "help"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ScriptDirBash = $ScriptDir -replace '\\', '/'

function Find-Bash {
    $onPath = Get-Command bash -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($candidate in @("C:\Program Files\Git\bin\bash.exe", "C:\msys64\usr\bin\bash.exe")) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$Bash = Find-Bash
if (-not $Bash) {
    Write-Error "bash.exe not found. Install Git for Windows (https://git-scm.com/download/win) or MSYS2 (https://www.msys2.org/), then re-run."
    exit 1
}

if ($Action -in @("run", "r", "start")) {
    # keyboard_input.exe needs a real Win32 console (SetConsoleMode/
    # ReadConsoleInput) - this PowerShell window is one, but a
    # double-clicked Git-Bash window (mintty) is not and will fail fast
    # with a clear error instead of hanging - see system/keyboard_input.c.
    Write-Host "Starting egg-pals - run this from a real console (this PowerShell/cmd window is fine; a Git-Bash/mintty window is not)." -ForegroundColor Yellow
}

& $Bash -lc "cd '$ScriptDirBash' && ./button.sh $Action"
exit $LASTEXITCODE
