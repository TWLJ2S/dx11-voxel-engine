param([string]$Path = (Join-Path $PSScriptRoot '..\assets\block\blocks.json'))

$ErrorActionPreference = 'Stop'
$text = [System.IO.File]::ReadAllText($Path)

function Set-BlockField([string]$Name, [string]$Field, [string]$JsonValue) {
    $script:changed = $false
    $escaped = [regex]::Escape($Name)
    $pattern = '(?ms)(^    \{\r?\n      "id": \d+,\r?\n      "name": "' +
        $escaped + '",)(.*?)(^    \}(?:,)?\r?$)'
    $script:text = [regex]::Replace($script:text, $pattern, {
        param($match)
        $body = $match.Groups[2].Value
        $fieldPattern = '(?m)^      "' + [regex]::Escape($Field) + '": .*?,?$'
        $line = '      "' + $Field + '": ' + $JsonValue + ','
        if ([regex]::IsMatch($body, $fieldPattern)) {
            $body = [regex]::Replace($body, $fieldPattern, $line, 1)
        }
        else {
            $body = "`r`n$line" + $body
        }
        $script:changed = $true
        return $match.Groups[1].Value + $body + $match.Groups[3].Value
    }, 1)
    if (-not $script:changed) { throw "Block '$Name' was not found" }
}

function Set-ModelFamily([string[]]$Names, [int]$Model) {
    foreach ($name in $Names) { Set-BlockField $name 'model' $Model }
}

$document = $text | ConvertFrom-Json
$names = @($document.blocks.name)

$trapdoors = @($names | Where-Object { $_ -match '_trapdoor$' })
Set-ModelFamily $trapdoors 24
foreach ($name in $trapdoors) { Set-BlockField $name 'interaction' '"trapdoor"' }

Set-ModelFamily @('end_rod','end_rod_inv','lightning_rod','lightning_rod_inv') 26
Set-ModelFamily @(
    'dragon_egg','sniffer_egg_not_cracked','sniffer_egg_not_cracked_east',
    'sniffer_egg_not_cracked_north','sniffer_egg_not_cracked_south',
    'sniffer_egg_not_cracked_west','turtle_egg','turtle_egg_slightly_cracked',
    'turtle_egg_very_cracked') 27
Set-ModelFamily @('ladder') 28
Set-ModelFamily @('campfire') 29
Set-ModelFamily @('bell') 30
Set-ModelFamily @('lever','lever_on') 31
Set-ModelFamily @('bamboo_fence_gate') 32
Set-BlockField 'bamboo_fence_gate' 'interaction' '"fence_gate"'
Set-BlockField 'bamboo_fence_gate' 'solid' 'true'
Set-BlockField 'bamboo_fence_gate' 'occludes' 'false'
Set-BlockField 'bamboo_fence_gate' 'renderMode' '"cutout"'
Set-ModelFamily @('lectern','lectern_base') 34
Set-ModelFamily @('grindstone','grindstone_pivot','grindstone_round') 35
Set-ModelFamily @('stonecutter','stonecutter_pe','stonecutter_saw') 36
Set-ModelFamily @(
    'sculk_sensor','calibrated_sculk_sensor_amethyst','sculk_sensor_tendril_active',
    'sculk_sensor_tendril_inactive','sculk_sensor_tendrill_active',
    'sculk_sensor_tendrill_inactive') 37
Set-ModelFamily @('scaffolding') 38
Set-ModelFamily @($names | Where-Object { $_ -match '_shelf$' }) 39
$shulkers = @($names | Where-Object { $_ -match '(^|_)shulker_box$' })
Set-ModelFamily $shulkers 40
foreach ($name in $shulkers) {
    Set-BlockField $name 'interaction' '"chest"'
    Set-BlockField $name 'occludes' 'false'
}
Set-ModelFamily @('pot') 41

$crossPlants = @(
    'closed_eyeblossom','open_eyeblossom','open_eyeblossom_emissive',
    'open_eyeblossom_emmisive','souls_glass_bottle','souls_glass_bottle_e')
Set-ModelFamily $crossPlants 15
foreach ($name in $crossPlants) {
    Set-BlockField $name 'solid' 'false'
    Set-BlockField $name 'occludes' 'false'
    Set-BlockField $name 'renderMode' $(if ($name -match 'glass_bottle') {
        '"translucent"'
    } else {
        '"cutout"'
    })
}

foreach ($name in @('sea_lantern','jack_o_lantern')) {
    Set-BlockField $name 'solid' 'true'
    Set-BlockField $name 'occludes' 'true'
    Set-BlockField $name 'renderMode' '"opaque"'
}

# RapidJSON intentionally rejects trailing commas. A field that was formerly
# last in an object may have gained one while being replaced above.
$text = [regex]::Replace(
    $text,
    ',(\r?\n    \}(?:,)?\r?$)',
    '$1',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)

[System.IO.File]::WriteAllText($Path, $text)

# Validate every texture reference after applying the semantic model families.
$updated = [System.IO.File]::ReadAllText($Path) | ConvertFrom-Json
foreach ($block in $updated.blocks) {
    $paths = @()
    if ($block.texture) { $paths += $block.texture }
    if ($block.textures) { $paths += @($block.textures.psobject.Properties.Value) }
    foreach ($texture in $paths) {
        $resolved = Join-Path (Split-Path $Path -Parent) "..\..\$texture"
        if (-not (Test-Path $resolved)) {
            throw "Missing texture for $($block.name): $texture"
        }
    }
}
