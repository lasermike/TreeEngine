[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\sun-shadow-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)
foreach ($variant in @('hard','soft')) {
    $scene = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Fixtures\sun-shadow.scene.json') -Raw | ConvertFrom-Json
    $scene.light.angularDiameterDegrees = if ($variant -eq 'hard') { 0 } else { 8 }
    $scenePath = Join-Path $OutputDirectory ($variant + '.scene.json')
    [IO.File]::WriteAllText($scenePath, ($scene | ConvertTo-Json -Depth 20), $utf8)
    $request = @{
        scene = $scenePath; mode = 'raytracing'; seed = 12345; width = 960; height = 540
        warmupFrames = 3; postProcessing = $false; report = (Join-Path $OutputDirectory ($variant + '-report.json'))
        captures = @(
            @{ time = 0; filename = (Join-Path $OutputDirectory ($variant + '.png')) },
            @{ time = 0; filename = (Join-Path $OutputDirectory ($variant + '-repeat.png')) }
        )
    }
    $requestPath = Join-Path $OutputDirectory ($variant + '-request.json')
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $OutputDirectory ($variant + '-stderr.log'))
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw "Capture timed out: $variant" }
    if ($process.ExitCode -ne 0) { throw "Capture failed: $variant; see stderr log" }
    $report = Get-Content -LiteralPath (Join-Path $OutputDirectory ($variant + '-report.json')) -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne 2) { throw "Incomplete captures: $variant" }
    $repeat = [TreeRenderTests.ImageComparison]::Compare((Join-Path $OutputDirectory ($variant + '.png')), (Join-Path $OutputDirectory ($variant + '-repeat.png')), (Join-Path $OutputDirectory ($variant + '-repeat-diff.png')))
    if (-not $repeat.SameDimensions -or $repeat.Rms -ne 0) { throw "Shadow capture is not repeatable: $variant" }
}
# Count intermediate brightness on the floor around the sphere's shadow.
# The hard fixture has a binary edge; a wide sun produces a partial shadow.
$counts = @{}
foreach ($variant in @('hard','soft')) {
    $image = [Drawing.Bitmap]::FromFile((Join-Path $OutputDirectory ($variant + '.png')))
    try {
        $partial = 0
        for ($y = 300; $y -lt 365; ++$y) {
            for ($x = 460; $x -lt 580; ++$x) {
                $red = $image.GetPixel($x, $y).R
                if ($red -gt 25 -and $red -lt 170) { ++$partial }
            }
        }
        $counts[$variant] = $partial
    } finally { $image.Dispose() }
}
if ($counts['soft'] -lt $counts['hard'] + 100) { throw "No soft shadow edge detected: hard=$($counts['hard']), soft=$($counts['soft'])" }
Write-Host "Sun shadow checks passed: hard=$($counts['hard']), soft=$($counts['soft']) partial pixels. Captures: $OutputDirectory"
