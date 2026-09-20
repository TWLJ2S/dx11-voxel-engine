param([string]$Path = (Join-Path $PSScriptRoot '..\assets\block\blocks.json'))

$ErrorActionPreference = 'Stop'
$document = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json

function Has-Field($Object, [string]$Name) {
    return $null -ne $Object.PSObject.Properties[$Name]
}

function Set-Missing($Object, [string]$Name, $Value) {
    if (-not (Has-Field $Object $Name)) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Contains([string]$Name, [string]$Token) { return $Name.Contains($Token) }

$food = @{
    apple=@(4.0,2.4); golden_apple=@(4.0,9.6); golden_carrot=@(6.0,14.4)
    bread=@(5.0,6.0); cooked_beef=@(8.0,12.8); cooked_porkchop=@(8.0,12.8)
    cooked_mutton=@(6.0,9.6); cooked_chicken=@(6.0,7.2); cooked_salmon=@(6.0,9.6)
    cooked_cod=@(5.0,6.0); cooked_rabbit=@(5.0,6.0); baked_potato=@(5.0,6.0)
    carrot=@(3.0,3.6); potato=@(1.0,0.6); cookie=@(2.0,0.4); melon_slice=@(2.0,1.2)
    beef=@(3.0,1.8); porkchop=@(3.0,1.8); chicken=@(2.0,1.2); mutton=@(2.0,1.2)
    mushroom_stew=@(6.0,7.2); beetroot_soup=@(6.0,7.2); rabbit_stew=@(10.0,12.0)
    pumpkin_pie=@(8.0,4.8); dried_kelp=@(1.0,0.6); dried_kelp_item=@(1.0,0.6)
    rotten_flesh=@(4.0,0.8); sweet_berries=@(2.0,0.4); beetroot=@(1.0,1.2)
    chorus_fruit=@(4.0,2.4)
}

$dyes = [ordered]@{
    light_blue=@(0.308,0.616,0.88); light_gray=@(0.616,0.616,0.616)
    orange=@(0.88,0.484,0.088); magenta=@(0.748,0.176,0.66); yellow=@(0.88,0.8096,0.132)
    lime=@(0.396,0.836,0.132); pink=@(0.88,0.484,0.616); gray=@(0.308,0.308,0.308)
    grey=@(0.308,0.308,0.308); cyan=@(0.132,0.66,0.704); purple=@(0.484,0.176,0.748)
    blue=@(0.132,0.22,0.792); brown=@(0.484,0.264,0.1056); green=@(0.176,0.572,0.132)
    red=@(0.792,0.1056,0.1056); black=@(0.0704,0.0704,0.0704); white=@(0.8096,0.8096,0.8096)
}

foreach ($block in $document.blocks) {
    $name = [string]$block.name
    $isItem = (Has-Field $block 'item') -and [bool]$block.item
    $has = { param([string]$token) return $name.Contains($token) }

    $tool = 'none'
    $toolLevel = -1
    $miningSpeed = 1.0
    $hardness = 1.0
    $harvestLevel = 0
    $requiresTool = $false

    if ($isItem) {
        $hardness = 0.0
        if (&$has 'pickaxe') { $tool = 'pickaxe' }
        elseif (&$has 'shovel') { $tool = 'shovel' }
        elseif (&$has 'hoe') { $tool = 'hoe' }
        elseif (&$has 'sword') { $tool = 'sword' }
        elseif (&$has 'axe') { $tool = 'axe' }
        if ($tool -ne 'none') {
            if (&$has 'diamond_') { $toolLevel=3; $miningSpeed=8.0 }
            elseif (&$has 'iron_') { $toolLevel=2; $miningSpeed=6.0 }
            elseif (&$has 'stone_') { $toolLevel=1; $miningSpeed=4.0 }
            elseif (&$has 'golden_') { $toolLevel=0; $miningSpeed=12.0 }
            elseif (&$has 'netherite_') { $toolLevel=4; $miningSpeed=9.0 }
            else { $toolLevel=0; $miningSpeed=2.0 }
        }
        if ($food.ContainsKey($name)) {
            Set-Missing $block 'food' ([ordered]@{ hunger=$food[$name][0]; saturation=$food[$name][1] })
        }
    }
    else {
        if ($name -in @('bedrock','barrier','water','lava')) { $hardness = -1.0 }
        elseif ((&$has 'leaves') -or (&$has 'snow') -or (&$has 'torch') -or (&$has 'sapling') -or
            (&$has 'flower') -or (&$has 'mushroom') -or ((&$has 'grass') -and -not (&$has 'grass_block') -and $name -ne 'grass')) { $hardness = 0.2 }
        elseif ((&$has 'glass') -or (&$has 'ice') -or (&$has 'slime')) { $hardness = 0.3 }
        elseif ((&$has 'dirt') -or (&$has 'sand') -or (&$has 'gravel') -or (&$has 'clay') -or
            (&$has 'farmland') -or (&$has 'soul_sand') -or (&$has 'soul_soil') -or $name -eq 'grass') { $hardness = 0.5 }
        elseif ((&$has 'wool') -or (&$has 'hay')) { $hardness = 0.8 }
        elseif (&$has 'obsidian') { $hardness = 50.0 }
        elseif ((&$has 'deepslate') -and (&$has 'ore')) { $hardness = 4.5 }
        elseif (&$has 'ore') { $hardness = 3.0 }
        elseif (&$has 'deepslate') { $hardness = 3.0 }
        elseif ((&$has 'log') -or (&$has 'plank') -or (&$has 'wood') -or (&$has 'crafting') -or
            (&$has 'chest') -or (&$has 'door') -or (&$has 'fence') -or (&$has 'slab') -or
            (&$has 'stairs') -or (&$has 'sign') -or (&$has 'barrel') -or (&$has 'bookshelf') -or (&$has 'ladder')) { $hardness = 2.0 }
        elseif ((&$has 'stone') -or (&$has 'cobble') -or (&$has 'brick') -or (&$has 'andesite') -or
            (&$has 'diorite') -or (&$has 'granite') -or (&$has 'basalt') -or (&$has 'tuff') -or
            (&$has 'nether') -or (&$has 'blackstone') -or (&$has 'copper') -or (&$has 'prismarine')) { $hardness = 1.5 }
        elseif ((&$has 'rail') -or (&$has 'redstone') -or (&$has 'repeater') -or (&$has 'comparator')) { $hardness = 0.0 }

        if (&$has 'obsidian') { $tool='pickaxe'; $requiresTool=$true; $harvestLevel=3 }
        elseif ((&$has 'diamond_ore') -or (&$has 'gold_ore') -or (&$has 'emerald_ore') -or (&$has 'redstone_ore')) {
            $tool='pickaxe'; $requiresTool=$true; $harvestLevel=2
        }
        elseif ((&$has 'iron_ore') -or (&$has 'lapis') -or (&$has 'copper_ore')) {
            $tool='pickaxe'; $requiresTool=$true; $harvestLevel=1
        }
        elseif ((&$has 'ore') -or (&$has 'stone') -or (&$has 'cobble') -or (&$has 'brick') -or
            (&$has 'deepslate') -or (&$has 'andesite') -or (&$has 'diorite') -or (&$has 'granite') -or
            (&$has 'basalt') -or (&$has 'tuff') -or (&$has 'nether_brick') -or (&$has 'blackstone') -or
            (&$has 'prismarine') -or (&$has 'terracotta') -or (&$has 'concrete')) {
            $tool='pickaxe'; $requiresTool=$true
        }
        elseif ((&$has 'dirt') -or (&$has 'sand') -or (&$has 'gravel') -or (&$has 'clay') -or
            (&$has 'snow') -or (&$has 'soul_sand') -or (&$has 'soul_soil') -or $name -eq 'grass' -or (&$has 'farmland')) { $tool='shovel' }
        elseif ((&$has 'log') -or (&$has 'plank') -or (&$has 'wood') -or (&$has 'crafting') -or
            (&$has 'chest') -or (&$has 'door') -or (&$has 'fence') -or (&$has 'sign') -or
            (&$has 'bookshelf') -or (&$has 'ladder') -or (&$has 'barrel') -or (&$has 'slab') -or (&$has 'stairs')) { $tool='axe' }
        elseif ((&$has 'leaves') -or (&$has 'wool') -or (&$has 'hay')) { $tool='hoe' }
    }

    if (Has-Field $block 'tool') { $tool = [string]$block.tool }
    if (Has-Field $block 'hardness') { $hardness = [double]$block.hardness }
    if (Has-Field $block 'harvestLevel') { $harvestLevel = [int]$block.harvestLevel; if ($isItem) { $toolLevel=$harvestLevel } }
    if (Has-Field $block 'toolHarvestLevel') { $toolLevel = [int]$block.toolHarvestLevel }
    if (Has-Field $block 'requiresTool') { $requiresTool = [bool]$block.requiresTool }
    if (Has-Field $block 'miningSpeed') { $miningSpeed = [double]$block.miningSpeed; if ($isItem -and $tool -ne 'none' -and $toolLevel -lt 0) { $toolLevel=0 } }

    Set-Missing $block 'hardness' $hardness
    Set-Missing $block 'harvestLevel' $harvestLevel
    Set-Missing $block 'tool' $tool
    Set-Missing $block 'requiresTool' $requiresTool
    Set-Missing $block 'miningSpeed' $miningSpeed
    if ($toolLevel -ge 0) { Set-Missing $block 'toolHarvestLevel' $toolLevel }

    $style = 'block'
    if (($isItem -and $tool -ne 'none') -or (&$has 'pickaxe') -or (&$has 'shovel') -or (&$has 'sword') -or (&$has 'hoe') -or
        (&$has 'axe') -or (&$has 'shears') -or (&$has 'trident') -or (&$has 'bow')) { $style='tool' }
    elseif ((&$has 'stick') -or (&$has '_rod') -or $name -eq 'bone' -or (&$has 'arrow') -or
        (&$has 'torch') -or (&$has 'blaze') -or $name -eq 'bamboo') { $style='rod' }
    elseif ([int]$block.model -eq 15 -or (&$has 'sapling') -or (&$has 'flower') -or (&$has 'fern') -or
        (&$has 'seagrass') -or (&$has 'dead_bush') -or (&$has 'tulip') -or (&$has 'orchid') -or
        (&$has 'poppy') -or (&$has 'dandelion') -or (&$has 'allium') -or (&$has 'bluet') -or
        (&$has 'daisy') -or (&$has 'lilac') -or (&$has 'peony') -or (&$has 'rose_bush') -or
        ((&$has 'grass') -and $name -ne 'grass' -and -not (&$has 'grass_block'))) { $style='cross' }
    elseif ([int]$block.model -eq 3 -or ((&$has '_slab') -and -not $isItem)) { $style='slab' }
    elseif ($isItem -or [int]$block.model -in @(8,16)) { $style='sprite' }
    Set-Missing $block 'heldStyle' $style

    $sound = 'stone'
    if ((&$has 'glass') -or (&$has 'ice')) { $sound='glass' }
    elseif ((&$has 'snow') -or (&$has 'powder')) { $sound='snow' }
    elseif (&$has 'sand') { $sound='sand' }
    elseif (&$has 'gravel') { $sound='gravel' }
    elseif ((&$has 'grass') -or (&$has 'leaves') -or (&$has 'moss') -or (&$has 'mycelium')) { $sound='grass' }
    elseif ((&$has 'wool') -or (&$has 'cloth') -or (&$has 'carpet')) { $sound='cloth' }
    elseif ((&$has 'log') -or (&$has 'plank') -or (&$has 'wood') -or (&$has 'door') -or
        (&$has 'chest') -or (&$has 'crafting') -or (&$has 'fence') -or (&$has 'slab') -or (&$has 'stairs')) { $sound='wood' }
    Set-Missing $block 'soundMaterial' $sound

    $armor = 0.0
    $points = if (&$has 'helmet') { 1.0 } elseif (&$has 'chestplate') { 3.0 } elseif (&$has 'leggings') { 2.0 } elseif (&$has 'boots') { 1.0 } else { 0.0 }
    if ($isItem -and $points -gt 0) {
        if ($name.StartsWith('diamond_')) { $points *= 2.0 }
        elseif ($name.StartsWith('iron_') -or $name.StartsWith('chainmail_')) { $points *= 1.5 }
        elseif ($name.StartsWith('golden_')) { $points *= 1.2 }
        $armor = $points * 0.04
    }
    Set-Missing $block 'armorReduction' $armor

    $glass = (&$has 'glass')
    Set-Missing $block 'connectsToPanes' ($glass -and [string]$block.renderMode -in @('transparent','translucent'))
    $transmission = @(1.0,1.0,1.0)
    if ($glass -and (&$has 'stained')) {
        foreach ($entry in $dyes.GetEnumerator()) {
            if (&$has $entry.Key) { $transmission=$entry.Value; break }
        }
    }
    Set-Missing $block 'lightTransmission' $transmission

    $flat = (&$has 'ladder') -or (&$has 'rail') -or (&$has 'lever') -or (&$has 'button') -or
        (&$has 'sign') -or (&$has 'banner') -or (&$has 'lantern') -or (&$has 'chain') -or (&$has 'coral_fan')
    Set-Missing $block 'flatIcon' $flat
}

$json = $document | ConvertTo-Json -Depth 50
[System.IO.File]::WriteAllText((Resolve-Path -LiteralPath $Path), $json + [Environment]::NewLine)
