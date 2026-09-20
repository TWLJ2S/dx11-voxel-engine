param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\model')
)

$ErrorActionPreference = 'Stop'

function New-Vertex([float[]]$Position, [float[]]$Normal, [float[]]$Uv) {
    [ordered]@{ position = $Position; normal = $Normal; uv = $Uv }
}

function Get-ProjectedUv([float[]]$Position, [float[]]$Normal) {
    $x = $Position[0]; $y = $Position[1]; $z = $Position[2]
    if ($Normal[0] -lt 0) { return [float[]]@($z, (1.0 - $y)) }
    if ($Normal[0] -gt 0) { return [float[]]@((1.0 - $z), (1.0 - $y)) }
    if ($Normal[1] -lt 0) { return [float[]]@($x, (1.0 - $z)) }
    if ($Normal[1] -gt 0) { return [float[]]@((1.0 - $x), (1.0 - $z)) }
    if ($Normal[2] -lt 0) { return [float[]]@((1.0 - $x), (1.0 - $y)) }
    return [float[]]@($x, (1.0 - $y))
}

function Add-Box($Mesh, [float]$X0, [float]$Y0, [float]$Z0,
    [float]$X1, [float]$Y1, [float]$Z1) {
    $base = $Mesh.vertices.Count
    $faces = @(
        @(@(-1,0,0), @($X0,$Y0,$Z0), @($X0,$Y0,$Z1), @($X0,$Y1,$Z1), @($X0,$Y1,$Z0)),
        @(@( 1,0,0), @($X1,$Y0,$Z1), @($X1,$Y0,$Z0), @($X1,$Y1,$Z0), @($X1,$Y1,$Z1)),
        @(@(0,-1,0), @($X0,$Y0,$Z0), @($X1,$Y0,$Z0), @($X1,$Y0,$Z1), @($X0,$Y0,$Z1)),
        @(@(0, 1,0), @($X1,$Y1,$Z0), @($X0,$Y1,$Z0), @($X0,$Y1,$Z1), @($X1,$Y1,$Z1)),
        @(@(0,0,-1), @($X1,$Y0,$Z0), @($X0,$Y0,$Z0), @($X0,$Y1,$Z0), @($X1,$Y1,$Z0)),
        @(@(0,0, 1), @($X0,$Y0,$Z1), @($X1,$Y0,$Z1), @($X1,$Y1,$Z1), @($X0,$Y1,$Z1))
    )
    foreach ($face in $faces) {
        [void]$Mesh.vertices.Add((New-Vertex $face[1] $face[0] (Get-ProjectedUv $face[1] $face[0])))
        [void]$Mesh.vertices.Add((New-Vertex $face[2] $face[0] (Get-ProjectedUv $face[2] $face[0])))
        [void]$Mesh.vertices.Add((New-Vertex $face[3] $face[0] (Get-ProjectedUv $face[3] $face[0])))
        [void]$Mesh.vertices.Add((New-Vertex $face[4] $face[0] (Get-ProjectedUv $face[4] $face[0])))
        [void]$Mesh.indices.Add($base); [void]$Mesh.indices.Add($base + 1); [void]$Mesh.indices.Add($base + 2)
        [void]$Mesh.indices.Add($base); [void]$Mesh.indices.Add($base + 2); [void]$Mesh.indices.Add($base + 3)
        $base += 4
    }
}

function New-Mesh { [ordered]@{ vertices = [System.Collections.ArrayList]::new(); indices = [System.Collections.ArrayList]::new() } }
function Save-Mesh([string]$Name, [uint32]$Id, $Mesh) {
    $path = Join-Path $OutputDirectory ($Name + '.json')
    $document = [ordered]@{ id = $Id; name = $Name; vertices = $Mesh.vertices; indices = $Mesh.indices }
    [System.IO.File]::WriteAllText($path, ($document | ConvertTo-Json -Depth 8))
}

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.0625 0.9375 0.625 0.9375
Add-Box $mesh 0.03125 0.625 0.03125 0.96875 0.875 0.96875
Save-Mesh 'chest' 19 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.0625 0.9375 0.5 0.9375
Save-Mesh 'cake' 20 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.125 0.0 0.125 0.875 0.125 0.875
Add-Box $mesh 0.125 0.125 0.125 0.3125 0.625 0.3125
Add-Box $mesh 0.6875 0.125 0.125 0.875 0.625 0.3125
Add-Box $mesh 0.125 0.125 0.6875 0.3125 0.625 0.875
Add-Box $mesh 0.6875 0.125 0.6875 0.875 0.625 0.875
Add-Box $mesh 0.0 0.625 0.0 1.0 0.875 1.0
Save-Mesh 'enchanting_table' 21 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.0 1.0 0.3125 1.0
Add-Box $mesh 0.0 0.3125 0.0 0.1875 1.0 1.0
Add-Box $mesh 0.8125 0.3125 0.0 1.0 1.0 1.0
Add-Box $mesh 0.1875 0.3125 0.0 0.8125 1.0 0.1875
Add-Box $mesh 0.1875 0.3125 0.8125 0.8125 1.0 1.0
Save-Mesh 'cauldron' 22 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.125 0.0 0.0625 0.875 0.25 0.9375
Add-Box $mesh 0.3125 0.25 0.25 0.6875 0.6875 0.75
Add-Box $mesh 0.0 0.6875 0.125 1.0 1.0 0.875
Save-Mesh 'anvil' 23 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.0 1.0 0.1875 1.0
Save-Mesh 'trapdoor' 24 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.8125 1.0 1.0 1.0
Save-Mesh 'trapdoor_open' 25 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.375 0.0 0.375 0.625 1.0 0.625
Add-Box $mesh 0.3125 0.0 0.3125 0.6875 0.125 0.6875
Save-Mesh 'rod' 26 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.3125 0.0 0.3125 0.6875 0.1875 0.6875
Add-Box $mesh 0.25 0.1875 0.25 0.75 0.5625 0.75
Add-Box $mesh 0.3125 0.5625 0.3125 0.6875 0.75 0.6875
Save-Mesh 'egg' 27 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.8125 1.0 1.0 1.0
Save-Mesh 'ladder' 28 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.1875 0.9375 0.1875 0.375
Add-Box $mesh 0.0625 0.0 0.625 0.9375 0.1875 0.8125
Add-Box $mesh 0.1875 0.1875 0.1875 0.8125 0.4375 0.8125
Save-Mesh 'campfire' 29 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.1875 0.75 0.1875 0.8125 0.875 0.8125
Add-Box $mesh 0.25 0.25 0.25 0.75 0.75 0.75
Add-Box $mesh 0.375 0.125 0.375 0.625 0.25 0.625
Save-Mesh 'bell' 30 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.25 0.0 0.25 0.75 0.125 0.75
Add-Box $mesh 0.4375 0.125 0.4375 0.5625 0.75 0.5625
Save-Mesh 'lever' 31 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.375 0.1875 1.0 0.625
Add-Box $mesh 0.8125 0.0 0.375 1.0 1.0 0.625
Add-Box $mesh 0.1875 0.25 0.4375 0.8125 0.4375 0.5625
Add-Box $mesh 0.1875 0.6875 0.4375 0.8125 0.875 0.5625
Save-Mesh 'fence_gate' 32 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.0 0.1875 1.0 0.25
Add-Box $mesh 0.0 0.0 0.75 0.1875 1.0 1.0
Add-Box $mesh 0.8125 0.0 0.0 1.0 1.0 0.25
Add-Box $mesh 0.8125 0.0 0.75 1.0 1.0 1.0
Save-Mesh 'fence_gate_open' 33 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.0625 0.9375 0.125 0.9375
Add-Box $mesh 0.375 0.125 0.375 0.625 0.6875 0.625
Add-Box $mesh 0.0625 0.6875 0.125 0.9375 0.875 0.9375
Save-Mesh 'lectern' 34 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.125 0.0 0.125 0.3125 0.75 0.875
Add-Box $mesh 0.6875 0.0 0.125 0.875 0.75 0.875
Add-Box $mesh 0.25 0.25 0.1875 0.75 1.0 0.8125
Save-Mesh 'grindstone' 35 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.0 1.0 0.5 1.0
Add-Box $mesh 0.0 0.5 0.4375 1.0 0.75 0.5625
Save-Mesh 'stonecutter' 36 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.0625 0.9375 0.5 0.9375
Add-Box $mesh 0.125 0.5 0.125 0.25 0.875 0.25
Add-Box $mesh 0.75 0.5 0.125 0.875 0.875 0.25
Add-Box $mesh 0.125 0.5 0.75 0.25 0.875 0.875
Add-Box $mesh 0.75 0.5 0.75 0.875 0.875 0.875
Save-Mesh 'sensor' 37 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.0 0.125 1.0 0.125
Add-Box $mesh 0.875 0.0 0.0 1.0 1.0 0.125
Add-Box $mesh 0.0 0.0 0.875 0.125 1.0 1.0
Add-Box $mesh 0.875 0.0 0.875 1.0 1.0 1.0
Add-Box $mesh 0.0 0.875 0.0 1.0 1.0 1.0
Save-Mesh 'scaffolding' 38 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0 0.0 0.125 1.0 0.125 0.875
Add-Box $mesh 0.0 0.4375 0.125 1.0 0.5625 0.875
Add-Box $mesh 0.0 0.875 0.125 1.0 1.0 0.875
Add-Box $mesh 0.0 0.125 0.125 0.125 0.875 0.875
Add-Box $mesh 0.875 0.125 0.125 1.0 0.875 0.875
Save-Mesh 'shelf' 39 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.0625 0.0 0.0625 0.9375 0.6875 0.9375
Add-Box $mesh 0.03125 0.6875 0.03125 0.96875 0.9375 0.96875
Save-Mesh 'shulker_box' 40 $mesh

$mesh = New-Mesh
Add-Box $mesh 0.25 0.0 0.25 0.75 0.1875 0.75
Add-Box $mesh 0.25 0.1875 0.25 0.3125 0.625 0.75
Add-Box $mesh 0.6875 0.1875 0.25 0.75 0.625 0.75
Add-Box $mesh 0.3125 0.1875 0.25 0.6875 0.625 0.3125
Add-Box $mesh 0.3125 0.1875 0.6875 0.6875 0.625 0.75
Save-Mesh 'flower_pot' 41 $mesh
