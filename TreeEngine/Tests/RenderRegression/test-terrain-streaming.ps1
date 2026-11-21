[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [string[]]$Modes = @('raster', 'raytracing'),
    [int]$Slots = 384
)
# Terrain streaming (Game/Terrain.h): a fast flight (650 m/s) over a generated 4 km terrain, then a hold. While flying, nodes
# load on the loader thread and a coarser node draws until its children arrive, so a few splits may wait; every frame
# must still draw terrain. Once the camera stops, everything it needs must be loaded: no split waiting and nothing
# pending. The same flight with streaming off loads synchronously and must never wait.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\terrain-streaming-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$utf8 = New-Object Text.UTF8Encoding($false)

# The terrain: TerrainTool's valley preset, 4 km at 1 m (7 levels).
$tool = Join-Path $root 'Tools\terrain\bin\Release\TerrainTool.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    & $msbuild (Join-Path $root 'Tools\terrain\TerrainTool.vcxproj') -p:Configuration=Release -p:Platform=x64 -v:minimal | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'TerrainTool build failed' }
}
$package = Join-Path $OutputDirectory 'Valley4k.terrain'
# TerrainTool reports progress on stderr, which PowerShell 5.1 would turn into a terminating error here.
$ErrorActionPreference = 'Continue'
& $tool generate --out $package --preset valley --size 4096 --spacing 1 --seed 7 --relief 400 2>&1 | Out-Null
$generated = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($generated -ne 0) { throw 'TerrainTool generate failed' }

# A scene for it, next to the built-in scenes while the test runs.
$sceneFile = Join-Path (Split-Path $Executable) 'Scenes\test-terrain-streaming.scene.json'
$scene = [ordered]@{
    version = 1; id = 'TestTerrainStreaming'; name = 'Terrain streaming test'
    camera = @{ position = @(-1600, 260, 0); rotationDegrees = @(20, 90, 0) }
    render = @{ farClippingPlane = 6000; useShadowMaps = $true; enablePostProcessing = $false }
    objects = @(@{ id = 'valley'; type = 'terrain'; source = ($package -replace '\\', '/'); lodRange = 2; slots = $Slots })
}
[IO.File]::WriteAllText($sceneFile, ($scene | ConvertTo-Json -Depth 8), $utf8)

function Invoke-Flight([string]$Mode, [bool]$Streaming, [string]$Job) {
    New-Item -ItemType Directory -Path $Job -Force | Out-Null
    $reportPath = Join-Path $Job 'report.json'
    # 2.6 km in 4 s, then 2 s still, facing +x and down so the view stays on the terrain.
    $request = @{
        scene = 'TestTerrainStreaming'; mode = $Mode; seed = 12345; width = 640; height = 360; postProcessing = $false
        terrainStreaming = $Streaming; report = $reportPath
        performance = @{ frames = 240; frameTimeStep = 0.025; startTime = 0; settleFrames = 1; camera = @(
            @{ time = 0; position = @(-1600, 260, 0); rotationDegrees = @(20, 90, 0) },
            @{ time = 4; position = @(1000, 260, 0); rotationDegrees = @(20, 90, 0) },
            @{ time = 6; position = @(1000, 260, 0); rotationDegrees = @(20, 90, 0) }) }
    }
    $requestPath = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
        -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(900000)) { $process.Kill(); $process.WaitForExit(); throw "Terrain streaming run timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Terrain streaming worker failed ($($process.ExitCode)): $Job" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success) { throw "Terrain streaming run did not complete: $Job" }
    return @($report.samples | Where-Object { -not $_.settle })
}

try {
    foreach ($mode in $Modes) {
        $streamed = Invoke-Flight $mode $true (Join-Path $OutputDirectory "$mode-streaming")
        $synchronous = Invoke-Flight $mode $false (Join-Path $OutputDirectory "$mode-synchronous")
        foreach ($samples in @($streamed, $synchronous)) {
            $empty = @($samples | Where-Object { $_.terrainNodes -eq 0 -or $_.terrainDrawnNodes -eq 0 })
            if ($empty.Count) { throw "$mode`: $($empty.Count) frames drew no terrain" }
        }
        $flying = @($streamed | Select-Object -First 160)
        $held = @($streamed | Select-Object -Last 40)
        $waiting = @($flying | Where-Object { $_.terrainMissingNodes -gt 0 }).Count
        $loads = ($streamed | Measure-Object -Property terrainLoadedNodes -Sum).Sum
        $late = @($held | Select-Object -Last 20 | Where-Object { $_.terrainMissingNodes -gt 0 -or $_.terrainPendingNodes -gt 0 })
        if ($late.Count) { throw "$mode`: terrain still loading $($late.Count) frames after the camera stopped" }
        $syncWaiting = @($synchronous | Where-Object { $_.terrainMissingNodes -gt 0 }).Count
        if ($syncWaiting) { throw "$mode`: synchronous loading left splits waiting in $syncWaiting frames" }
        $maxResident = ($streamed | Measure-Object -Property terrainResidentNodes -Maximum).Maximum
        if ($maxResident -gt $Slots) { throw "$mode`: $maxResident resident nodes exceed $Slots slots" }
        "{0}: streamed {1} loads; {2} of {3} flying frames waited on a split; settled when stopped; {4} resident at most" -f `
            $mode, $loads, $waiting, $flying.Count, $maxResident
    }
}
finally {
    Remove-Item -LiteralPath $sceneFile -Force -ErrorAction SilentlyContinue
}
"Terrain streaming checks passed. Runs: $OutputDirectory"
