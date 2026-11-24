[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
# Roadside props in the terrain-roads scene, in raster and DXR: Ctrl-click picking (a click on a prop selects its
# terrain and the prop the click's ray hits; on bare ground, the terrain and no prop; on the sky, nothing), then edits
# as the debug UI makes them (move a stop sign 3 m east and turn it, remove a mailbox): the moved prop stands on the
# ground at its new place facing its new heading, the removed one is gone, and the picture changes. Nothing is saved.
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$executable = Join-Path $workspace 'TreeClassic\bin\x64\Debug\TreeClassic.exe'
$artifacts = Join-Path $PSScriptRoot 'Artifacts\PropEditing'
[void][IO.Directory]::CreateDirectory($artifacts)
$sign = 'prop-street-504474-5228555'
$stop = 'prop-stop-504474-5228555-330'
$mailbox = 'prop-mailbox-osm-way-975904216'
foreach ($mode in @('raster', 'raytracing')) {
    $steps = @(
        @{ id = 'sign'; clicks = @(@{u=0.66;v=0.38}); expected = @('ground'); prop = $sign },
        # The blade's last 15 cm: E MOONLIGHT OASIS's blade is drawn 1.2 m long, past the model's 0.9 m.
        @{ id = 'blade-tip'; clicks = @(@{u=0.58;v=0.382}); expected = @('ground'); prop = $sign },
        @{ id = 'ground'; clicks = @(@{u=0.2;v=0.8}); expected = @('ground'); prop = '' },
        @{ id = 'sky'; clicks = @(@{u=0.5;v=0.05}); expected = @(); prop = '' },
        @{ id = 'before'; clicks = @(); expected = @(); prop = '' },
        @{ id = 'edited'; clicks = @(); expected = @(); prop = ''
           edits = @(@{ id = $stop; move = @(3, 0); heading = 90 }, @{ id = $mailbox; remove = $true }) },
        # A mailbox 70 m off behind a rise: the click hits the hillside in front of it, so no prop. (Last: its camera
        # stays for the captures after it.)
        @{ id = 'occluded'; clicks = @(@{u=0.5;v=0.5}); expected = @('ground'); prop = ''
           camera = @(91.0, 46.08, -42.7); rotation = @(2.4, 10.0, 0) }
    )
    # The save goes to a copy of the roads file: only the two edited props' lines may change.
    $source = Join-Path $workspace 'Game\Resources\Terrain\RoadsTest.roads.geojson'
    $saved = Join-Path $artifacts "$mode-saved.roads.geojson"
    Copy-Item $source $saved -Force
    $captures = @()
    foreach ($step in $steps) {
        $capture = @{ time = 0; filename = (Join-Path $artifacts "$mode-$($step.id).png")
            selectionClicks = @($step.clicks); expectedSelection = @($step.expected); expectedProp = $step.prop }
        if ($step.ContainsKey('edits')) { $capture.propEdits = @($step.edits); $capture.savePropsTo = $saved }
        if ($step.ContainsKey('camera')) { $capture.cameraPosition = $step.camera; $capture.cameraRotationDegrees = $step.rotation }
        $captures += $capture
    }
    $report = Join-Path $artifacts "$mode-report.json"
    $request = @{ scene = 'LoadTerrainRoads'; mode = $mode; width = 640; height = 480; warmupFrames = 6; report = $report; captures = $captures }
    $requestPath = Join-Path $artifacts "$mode-request.json"
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 10))
    $before = (Get-FileHash (Join-Path $workspace 'Game\Resources\Terrain\RoadsTest.roads.geojson')).Hash
    $process = Start-Process -FilePath $executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $artifacts "$mode-stderr.log")
    [void]$process.Handle
    if (!$process.WaitForExit(120000)) { $process.Kill(); throw "$mode prop editing test timed out" }
    if ($process.ExitCode -ne 0) {
        $details = if (Test-Path $report) { (Get-Content -Raw $report | ConvertFrom-Json).error } else { 'See stderr log.' }
        throw "$mode prop editing test failed: $details"
    }
    $result = Get-Content -Raw $report | ConvertFrom-Json
    $edited = ($result.captures | Where-Object { $_.props.Count })[-1].props
    $moved = $edited | Where-Object { $_.id -eq $stop }
    $gone = $edited | Where-Object { $_.id -eq $mailbox }
    if (!$moved -or [math]::Abs($moved.heading - 90) -gt 0.01 -or [math]::Abs($moved.bottom - $moved.ground) -gt 0.05 -or $moved.height -lt 2.5) {
        throw "$mode prop editing: the moved stop sign isn't standing on the ground at its new heading: $($moved | ConvertTo-Json -Compress)"
    }
    if (!$gone -or $gone.height -ne 0) { throw "$mode prop editing: the removed mailbox is still drawn: $($gone | ConvertTo-Json -Compress)" }
    if ((Get-FileHash (Join-Path $artifacts "$mode-before.png")).Hash -eq (Get-FileHash (Join-Path $artifacts "$mode-edited.png")).Hash) {
        throw "$mode prop editing: the edits didn't change the picture"
    }
    if ((Get-FileHash $source).Hash -ne $before) {
        throw "$mode prop editing: the roads file changed (the test saves only into its copy)"
    }
    $old = Get-Content $source
    $new = Get-Content $saved
    $changed = @(for ($i = 0; $i -lt [math]::Max($old.Count, $new.Count); $i++) { if ($old[$i] -ne $new[$i]) { $i } })
    $stopLine = $new | Where-Object { $_ -like "*`"id`": `"$stop`"*" }
    $mailboxLine = $new | Where-Object { $_ -like "*`"id`": `"$mailbox`"*" }
    $stopSaved = $stopLine -like '*"edited": true*' -and $stopLine -like '*"heading": 90.0*'
    $mailboxSaved = $mailboxLine -like '*"removed": true*' -and $mailboxLine -like '*"edited": true*'
    if ($changed.Count -ne 2 -or !$stopSaved -or !$mailboxSaved) {
        throw "$mode prop editing: the saved file changed $($changed.Count) lines (expected the two edited props):`n$stopLine`n$mailboxLine"
    }
    Write-Host "$mode prop editing: picking and edits passed (stop sign moved to x=$([math]::Round($moved.x, 2)), on the ground at $([math]::Round($moved.ground, 2)) m)"
}
