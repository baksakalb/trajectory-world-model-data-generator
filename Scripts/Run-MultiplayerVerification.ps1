param(
    [ValidateSet("Zero", "Jitter75", "Lag200", "Loss1", "Loss5", "Loss10")]
    [string]$Profile = "Zero",
    [int]$DurationSeconds = 42,
    [switch]$Reconnect,
    [switch]$Rendered,
    [switch]$NetworkTrace,
    [string]$PackagedExecutable = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot "he_grenade_game.uproject"
$editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if (!$PackagedExecutable -and !(Test-Path -LiteralPath $editor)) {
    throw "UE 5.8 editor was not found at $editor"
}
if ($PackagedExecutable -and !(Test-Path -LiteralPath $PackagedExecutable)) {
    throw "Packaged executable was not found at $PackagedExecutable"
}
if ($PackagedExecutable) {
    $packagedRoot = Split-Path -Parent $PackagedExecutable
    $packagedName = [System.IO.Path]::GetFileNameWithoutExtension($PackagedExecutable)
    $innerExecutable = Join-Path `
        $packagedRoot `
        "$packagedName\Binaries\Win64\$packagedName.exe"
    if (Test-Path -LiteralPath $innerExecutable) {
        # The archive-root executable is a bootstrapper. Launch the inner binary
        # directly so the test owns and reliably terminates the actual process.
        $PackagedExecutable = $innerExecutable
    }
}
$launchExecutable = if ($PackagedExecutable) { $PackagedExecutable } else { $editor }

$profileArguments = @{
    Zero     = ""
    Jitter75 = "-PktLag=75 -PktLagVariance=25"
    Lag200   = "-PktLag=200"
    Loss1    = "-PktLoss=1"
    Loss5    = "-PktLoss=5"
    Loss10   = "-PktLoss=10"
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $projectRoot "Saved\NetworkVerification\$Profile-$stamp"
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

function Start-GameProcess {
    param(
        [string]$TravelUrl,
        [string]$LogName,
        [string]$AdditionalArguments = ""
    )

    $logPath = Join-Path $runRoot $LogName
    $windowX = if ($LogName -eq "Host.log") { 0 } else { 960 }
    $renderArgument = if ($Rendered) {
        "-windowed -ResX=960 -ResY=540 -WinX=$windowX -WinY=0"
    }
    else {
        "-nullrhi"
    }
    $traceArguments = ""
    if ($NetworkTrace) {
        $tracePath = Join-Path $runRoot ($LogName + ".utrace")
        $traceArguments = "-trace=net -tracefile=`"$tracePath`" -ExecCmds=`"stat net,netprofile enable`""
    }

    $arguments = @(
        $(if (!$PackagedExecutable) { "`"$projectFile`"" })
        "`"$TravelUrl`""
        "-game"
        "-unattended"
        "-nosound"
        "-NoSplash"
        $renderArgument
        "-CustomConfig=LocalMultiplayer"
        "-GGNetworkSelfTest"
        "-GGAuthorityScenarios"
        "-GGFastCollapse"
        "-GGNetworkMetrics"
        "-GGArenaSeed=424242"
        $profileArguments[$Profile]
        $traceArguments
        $AdditionalArguments
        "-abslog=`"$logPath`""
    ) -join " "

    $startParameters = @{
        FilePath = $launchExecutable
        ArgumentList = $arguments
        PassThru = $true
    }
    if (!$Rendered) {
        $startParameters["WindowStyle"] = "Hidden"
    }
    $process = Start-Process @startParameters
    $ownedProcesses.Add($process)
    return $process
}

function Wait-ForLogPattern {
    param(
        [string]$Path,
        [string]$Pattern,
        [int]$TimeoutSeconds = 90
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path -LiteralPath $Path) -and
            (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Stop-OwnedProcess {
    param([System.Diagnostics.Process]$Process)
    if ($Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $Process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
}

function Get-MatchCount {
    param([string]$Path, [string]$Pattern)
    if (!(Test-Path -LiteralPath $Path)) {
        return 0
    }
    return @(Select-String -LiteralPath $Path -Pattern $Pattern -AllMatches).Count
}

function Get-LastRevision {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        return -1
    }

    $lastRevision = -1
    foreach ($match in Select-String -LiteralPath $Path -Pattern "global_revision=(\d+)" -AllMatches) {
        foreach ($capture in $match.Matches) {
            $lastRevision = [Math]::Max($lastRevision, [int]$capture.Groups[1].Value)
        }
    }
    return $lastRevision
}

function Get-ArenaStateMap {
    param([string]$Path)
    $stateMap = @{}
    if (!(Test-Path -LiteralPath $Path)) {
        return $stateMap
    }

    foreach ($lineMatch in Select-String `
        -LiteralPath $Path `
        -Pattern "ARENA_STATE role=\w+ id=(\d+) state=(\w+) item_revision=(\d+)") {
        foreach ($capture in $lineMatch.Matches) {
            $arenaId = [int]$capture.Groups[1].Value
            $stateMap[$arenaId] =
                "$($capture.Groups[2].Value):$($capture.Groups[3].Value)"
        }
    }
    return $stateMap
}

function Test-ArenaStateParity {
    param(
        [hashtable]$AuthorityState,
        [hashtable]$MirrorState
    )
    if ($AuthorityState.Count -ne $MirrorState.Count) {
        return $false
    }
    foreach ($arenaId in $AuthorityState.Keys) {
        if (!$MirrorState.ContainsKey($arenaId) -or
            $MirrorState[$arenaId] -ne $AuthorityState[$arenaId]) {
            return $false
        }
    }
    return $true
}

$hostLog = Join-Path $runRoot "Host.log"
$clientLog = Join-Path $runRoot "Client.log"
$reconnectLog = Join-Path $runRoot "Reconnect.log"
$hostProcess = $null
$clientProcess = $null
$replacementClientProcess = $null

try {
    $hostProcess = Start-GameProcess `
        -TravelUrl "/Game/FirstPerson/Lvl_FirstPerson?listen" `
        -LogName "Host.log"
    if (!(Wait-ForLogPattern -Path $hostLog -Pattern "IpNetDriver listening on port 7777")) {
        throw "Listen host did not become ready. See $hostLog"
    }

    $clientProcess = Start-GameProcess -TravelUrl "127.0.0.1:7777" -LogName "Client.log"
    if (!(Wait-ForLogPattern -Path $clientLog -Pattern "ARENA_SNAPSHOT mirror")) {
        throw "Client did not receive the arena snapshot. See $clientLog"
    }

    if ($Reconnect) {
        $preDisconnectSeconds = [Math]::Min(15, [Math]::Max(8, $DurationSeconds - 20))
        Start-Sleep -Seconds $preDisconnectSeconds
        Stop-OwnedProcess -Process $clientProcess
        if (!(Wait-ForLogPattern -Path $hostLog -Pattern "MATCH_PHASE role=authority phase=4" -TimeoutSeconds 15)) {
            throw "Host did not enter reconnect grace after the client exited."
        }
        $replacementClientProcess = Start-GameProcess -TravelUrl "127.0.0.1:7777" -LogName "Reconnect.log"
        if (!(Wait-ForLogPattern -Path $reconnectLog -Pattern "ARENA_SNAPSHOT mirror")) {
            throw "Replacement client did not receive the late-join snapshot."
        }
        Start-Sleep -Seconds ([Math]::Max(20, $DurationSeconds - $preDisconnectSeconds))
    }
    else {
        Start-Sleep -Seconds $DurationSeconds
    }
}
finally {
    Stop-OwnedProcess -Process $replacementClientProcess
    Stop-OwnedProcess -Process $clientProcess
    Stop-OwnedProcess -Process $hostProcess
}

$activeClientLog = if ($Reconnect) { $reconnectLog } else { $clientLog }
$hostRevision = Get-LastRevision -Path $hostLog
$clientRevision = Get-LastRevision -Path $activeClientLog
$hostArenaState = Get-ArenaStateMap -Path $hostLog
$clientArenaState = Get-ArenaStateMap -Path $activeClientLog
$forbiddenPattern =
    "No owning connection|will not be processed|NetGUID.*(fail|unresolved)|" +
    "movement base.*(fail|unresolved)|Reliable buffer overflow|reliable partial bunch overflows"

$checks = [ordered]@{
    ListenHostStarted = (Get-MatchCount $hostLog "IpNetDriver listening on port 7777") -ge 1
    ClientSnapshotApplied = (Get-MatchCount $activeClientLog "ARENA_SNAPSHOT mirror") -ge 1
    TwoPlayersReady = (Get-MatchCount $hostLog "ARENA_READY_ACCEPT") -ge 2
    MatchStarted = (Get-MatchCount $hostLog "MATCH_PHASE role=authority phase=3") -ge 1
    BothThrowGatewaysAccepted = (Get-MatchCount $hostLog "GRENADE_THROW_ACCEPT") -ge 2
    LocalPredictionSpawned = (Get-MatchCount $clientLog "GRENADE_PREDICTION_SPAWN") -ge 1
    LocalPredictionReconciled = (Get-MatchCount $clientLog "GRENADE_RECONCILE_COMPLETE") -ge 1
    ArenaWasMutatedByAuthority = $hostRevision -gt 0
    ArenaRevisionParity = ($hostRevision -eq $clientRevision)
    ExactArenaStateParity =
        (Test-ArenaStateParity $hostArenaState $clientArenaState)
    HostUnderSelf = (Get-MatchCount $hostLog "Scenario=HostUnderSelf") -ge 1
    HostUnderClient = (Get-MatchCount $hostLog "Scenario=HostUnderClient") -ge 1
    ClientUnderSelf = (Get-MatchCount $hostLog "Scenario=ClientUnderSelf") -ge 1
    ClientUnderHost = (Get-MatchCount $hostLog "Scenario=ClientUnderHost") -ge 1
    SimultaneousThrows =
        (Get-MatchCount $hostLog "Scenario=SimultaneousHost") -ge 1 -and
        (Get-MatchCount $hostLog "Scenario=SimultaneousClient") -ge 1
    BreakableObstacleBounce =
        (Get-MatchCount $hostLog "GRENADE_BOUNCE.*ArenaType=1") -ge 1
    StaticObstacleBounce =
        (Get-MatchCount $hostLog "GRENADE_BOUNCE.*ArenaType=2") -ge 1
    MovementBaseInvalidated =
        (Get-MatchCount $hostLog "ARENA_BASE_INVALIDATE") -ge 1
    StandardFallingObserved =
        (Get-MatchCount $hostLog "CHARACTER_MOVEMENT_MODE.*previous=1 current=3") -ge 1
    DeathObserved = (Get-MatchCount $hostLog "PLAYER_ELIMINATED") -ge 1
    DelayedRespawnObserved = (Get-MatchCount $hostLog "PLAYER_RESPAWNED") -ge 1
    FullFloorCollapseCompleted = (Get-MatchCount $hostLog "FLOOR_COLLAPSE_COMPLETE") -ge 1
    NoForbiddenNetworkWarnings =
        ((Get-MatchCount $hostLog $forbiddenPattern) +
         (Get-MatchCount $clientLog $forbiddenPattern) +
         (Get-MatchCount $reconnectLog $forbiddenPattern)) -eq 0
}

if ($Reconnect) {
    $checks["ReconnectGraceObserved"] =
        (Get-MatchCount $hostLog "MATCH_PHASE role=authority phase=4") -ge 1
    $checks["ReconnectResumedMatch"] =
        (Get-MatchCount $hostLog "MATCH_PHASE role=authority phase=3") -ge 2
    $checks["LateJoinReceivedDestroyedState"] =
        (Get-MatchCount $reconnectLog "ARENA_STATE role=mirror") -ge 1
}

$passed = !($checks.Values -contains $false)
$summary = [ordered]@{
    Profile = $Profile
    Reconnect = [bool]$Reconnect
    Rendered = [bool]$Rendered
    Packaged = [bool]$PackagedExecutable
    HostArenaRevision = $hostRevision
    ClientArenaRevision = $clientRevision
    HostDestroyedStateCount = $hostArenaState.Count
    ClientDestroyedStateCount = $clientArenaState.Count
    Checks = $checks
    Passed = $passed
    RunDirectory = $runRoot
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot "Summary.json")
$summary | ConvertTo-Json -Depth 5

if (!$passed) {
    exit 1
}
