<#
.SYNOPSIS
    Offline check of the Virtual Hand models' skeletons: composes the bone transforms of both .i3d files the way the
    engine does and compares them with the armature of the source Blender file, scaled to metres and, for the right
    hand, mirrored.

.DESCRIPTION
    The engine takes the bind pose of a skinned shape from the world transforms of its bone nodes as loaded
    (the .shapes file holds no bind matrices), so the skeleton the .i3d composes to must be the skeleton the mesh
    was modelled around. The first export of the Gloved Hand was not: the GIANTS Blender exporter wrote every
    bone with 90 degrees too little about X, the 90 degree turns piled up down each finger, and the composed
    wrist-to-fingertip chain was 0.46 model units long instead of 2.25 (0.040 m instead of 0.197 m in the game).

    The engine also ignores a scale (or a mirror) on a node above a skinned mesh, so the models carry their size
    and handedness themselves: VirtualHandLeft.i3d is the glove scaled to metres (0.0875 m per model unit) and
    VirtualHandRight.i3d is that mirrored across the model's X = 0 plane, with the same bone names.

    This script reads each model, composes every bone's world matrix (Euler angles in degrees, R = Rz * Ry * Rx, the
    convention the exporter writes; the check also composes R = Ry * Rx * Rz and reports how far the two disagree),
    and checks:
      * every bone is within -Tolerance metres of where the Blender armature has it at rest (the numbers are the
        bone heads of "Gloved Hand LP.blend" in the exporter's Y-up axes: x, z, -y, times 0.0875, with x negated
        for the right hand),
      * the wrist to the first joint of the index and middle fingertip segments ("Top.001", "Top.002") is about
        0.197 and 0.204 m,
      * no finger spirals: its chain runs nearly straight from the Hand bone to its Top joint,
      * every bone's local rotation is a proper rotation with small bends: the right file's rotations are the left
        file's with the Euler Y and Z angles negated (a mirrored rig), and no rotation is a 90 degree pile-up.
    It exits with 1 when a check fails. With -Curl it also folds the fingers by VRCompanion.FINGER_CURL_DEGREES
    (read from VRCompanionVirtualHands.lua) and prints where the fingertips end up relative to the palm for each
    hand; a curl that does not move the fingertips toward the palm (-Y) fails.

    Run it from the repository root:  powershell -File scripts\check_virtual_hand_rig.ps1 [-Curl] [-Quiet]
    The same expectations are asserted by CompanionModTest (test_virtual_hands.cpp), which runs in CI.
#>
param(
    [string]$ModelsDirectory = "mods/FS25_VRCompanion/models",
    [string]$Source = "mods/FS25_VRCompanion/src/VRCompanionVirtualHands.lua",
    [double]$Tolerance = 0.0005,
    [double]$MetresPerUnit = 0.0875,
    [switch]$Curl,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

# Bone heads of the Blender armature (rest pose, in the model's original units), in the exporter's Y-up axes.
$Expected = [ordered]@{
    "Wrist" = @(0.0630, 0.0551, 0.9691); "Hand" = @(0.0630, 0.0551, 0.5246)
    "Thumb" = @(0.0695, 0.0551, -0.1343); "Lower" = @(0.5854, 0.0551, 0.0182)
    "Middle" = @(0.7947, 0.0111, -0.1689); "Top" = @(0.9515, -0.0800, -0.4387)
    "Index Finger" = @(0.0695, 0.0551, -0.1343); "Lower.001" = @(0.3777, 0.0551, -0.6210)
    "Middle.001" = @(0.4523, 0.0218, -0.9363); "Top.001" = @(0.5042, -0.0903, -1.2298)
    "Middle Finger" = @(0.0695, 0.0551, -0.1343); "Lower.002" = @(0.1051, 0.0551, -0.7313)
    "Middle.002" = @(0.1343, 0.0317, -1.0804); "Top.002" = @(0.1603, -0.0746, -1.3587)
    "Ring Finger" = @(0.0695, 0.0551, -0.1343); "Lower.003" = @(-0.1674, 0.0551, -0.6794)
    "Middle.003" = @(-0.1902, 0.0418, -0.9521); "Top.003" = @(-0.2127, -0.0885, -1.2401)
    "Pinky" = @(0.0695, 0.0551, -0.1343); "Lower.004" = @(-0.3816, 0.0551, -0.6307)
    "Middle.004" = @(-0.4387, 0.0330, -0.7780); "Top.004" = @(-0.5405, -0.0757, -0.9996)
}
$Fingers = [ordered]@{
    "index" = @("Index Finger", "Lower.001", "Middle.001", "Top.001")
    "middle" = @("Middle Finger", "Lower.002", "Middle.002", "Top.002")
    "ring" = @("Ring Finger", "Lower.003", "Middle.003", "Top.003")
    "little" = @("Pinky", "Lower.004", "Middle.004", "Top.004")
}

# --- small 3x3 matrix helpers (row-major, 9 doubles) ---------------------------------------------------------
function Mul([double[]]$a, [double[]]$b) {
    $r = New-Object double[] 9
    for ($i = 0; $i -lt 3; $i++) { for ($j = 0; $j -lt 3; $j++) {
        $r[3 * $i + $j] = $a[3 * $i] * $b[$j] + $a[3 * $i + 1] * $b[3 + $j] + $a[3 * $i + 2] * $b[6 + $j]
    } }
    return , $r
}
function Apply([double[]]$m, [double[]]$v) {
    return , @(($m[0] * $v[0] + $m[1] * $v[1] + $m[2] * $v[2]), ($m[3] * $v[0] + $m[4] * $v[1] + $m[5] * $v[2]), ($m[6] * $v[0] + $m[7] * $v[1] + $m[8] * $v[2]))
}
function RotX([double]$a) { $c = [Math]::Cos($a); $s = [Math]::Sin($a); return , @(1, 0, 0, 0, $c, - $s, 0, $s, $c) }
function RotY([double]$a) { $c = [Math]::Cos($a); $s = [Math]::Sin($a); return , @($c, 0, $s, 0, 1, 0, - $s, 0, $c) }
function RotZ([double]$a) { $c = [Math]::Cos($a); $s = [Math]::Sin($a); return , @($c, - $s, 0, $s, $c, 0, 0, 0, 1) }
function Euler([double[]]$deg, [string]$order) {
    $x = RotX ($deg[0] * [Math]::PI / 180); $y = RotY ($deg[1] * [Math]::PI / 180); $z = RotZ ($deg[2] * [Math]::PI / 180)
    if ($order -eq "ZYX") { return , (Mul (Mul $z $y) $x) }   # R = Rz * Ry * Rx, the exporter's XYZ
    return , (Mul (Mul $y $x) $z)                              # R = Ry * Rx * Rz
}
function Distance([double[]]$a, [double[]]$b) {
    return [Math]::Sqrt([Math]::Pow($a[0] - $b[0], 2) + [Math]::Pow($a[1] - $b[1], 2) + [Math]::Pow($a[2] - $b[2], 2))
}
function Numbers([string]$text, [string]$fallback) {
    if ([string]::IsNullOrEmpty($text)) { $text = $fallback }
    return , @($text.Trim() -split "\s+" | ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })
}

# --- read a model ------------------------------------------------------------------------------------------------
function ReadBones([string]$path) {
    if (-not (Test-Path $path)) { throw "model not found: $path" }
    [xml]$doc = Get-Content -Raw $path
    $script:bones = [ordered]@{}   # name -> @{ parent; t; deg; world = @{ ZYX = (R, p); YXZ = (R, p) } }
    function Walk($node, $parent) {
        foreach ($child in $node.ChildNodes) {
            if ($child.LocalName -ne "TransformGroup") { continue }
            $t = Numbers $child.translation "0 0 0"
            $deg = Numbers $child.rotation "0 0 0"
            $scale = Numbers $child.scale "1 1 1"
            if (($scale | Where-Object { [Math]::Abs($_ - 1) -gt 1e-6 }).Count -gt 0) { $script:failures.Add("bone '$($child.name)' has a scale: no node of a skinned model may") }
            $entry = @{ parent = $parent; t = $t; deg = $deg; world = @{} }
            foreach ($order in "ZYX", "YXZ") {
                $local = Euler $deg $order
                if ($parent -eq $null) {
                    $entry.world[$order] = @($local, $t)
                } else {
                    $p = $script:bones[$parent].world[$order]
                    $offset = Apply $p[0] $t
                    $entry.world[$order] = @((Mul $p[0] $local), @(($p[1][0] + $offset[0]), ($p[1][1] + $offset[1]), ($p[1][2] + $offset[2])))
                }
            }
            $script:bones[$child.name] = $entry
            Walk $child $child.name
        }
    }
    Walk $doc.i3D.Scene.TransformGroup $null
    return $script:bones
}

$failures = New-Object System.Collections.Generic.List[string]
function Check([bool]$ok, [string]$message) { if (-not $ok) { $failures.Add($message) } }

$models = [ordered]@{ "left" = (Join-Path $ModelsDirectory "VirtualHandLeft.i3d"); "right" = (Join-Path $ModelsDirectory "VirtualHandRight.i3d") }
$allBones = @{}
$fronts = @{}
foreach ($hand in $models.Keys) {
    Write-Host ("`n=== {0} hand: {1}" -f $hand, $models[$hand])
    $mirror = if ($hand -eq "right") { -1.0 } else { 1.0 }
    $bones = ReadBones $models[$hand]
    $allBones[$hand] = $bones

    # --- 1. every bone where the armature has it (metres, mirrored for the right hand) -----------------------
    $worst = 0.0; $worstOther = 0.0
    foreach ($name in $Expected.Keys) {
        if (-not $bones.Contains($name)) { $failures.Add("the $hand model has no bone '$name'"); continue }
        $head = $Expected[$name]
        $want = @(($mirror * $head[0] * $MetresPerUnit), ($head[1] * $MetresPerUnit), ($head[2] * $MetresPerUnit))
        $got = $bones[$name].world["ZYX"][1]
        $err = Distance $got $want
        $worst = [Math]::Max($worst, $err)
        $worstOther = [Math]::Max($worstOther, (Distance $bones[$name].world["YXZ"][1] $want))
        Check ($err -le $Tolerance) ("$hand bone '$name' composes to ({0:N4}, {1:N4}, {2:N4}) m, the armature has it at ({3:N4}, {4:N4}, {5:N4}) m" -f $got[0], $got[1], $got[2], $want[0], $want[1], $want[2])
        if (-not $Quiet) {
            Write-Host ("{0,-14} parent {1,-14} world ({2,8:N4} {3,8:N4} {4,8:N4}) m   local rotation ({5,9:N3} {6,9:N3} {7,9:N3}) deg   error {8:N6}" -f `
                $name, $bones[$name].parent, $got[0], $got[1], $got[2], $bones[$name].deg[0], $bones[$name].deg[1], $bones[$name].deg[2], $err)
        }
    }
    Write-Host ("worst bone position error: {0:N6} m (R = Rz*Ry*Rx); {1:N6} m if the engine composes R = Ry*Rx*Rz instead" -f $worst, $worstOther)
    Check ($worstOther -le 5 * $Tolerance) "the two Euler conventions disagree by $worstOther m in the ${hand}: the model is not convention independent"

    # --- 2. wrist to fingertip chain ---------------------------------------------------------------------------
    $wrist = $bones["Wrist"].world["ZYX"][1]
    foreach ($tip in @(@("Top.001", 0.197), @("Top.002", 0.204))) {
        $length = Distance $wrist $bones[$tip[0]].world["ZYX"][1]
        Write-Host ("Wrist to {0}: {1:N4} m (armature: {2:N3} m)" -f $tip[0], $length, $tip[1])
        Check ([Math]::Abs($length - $tip[1]) -le 0.005) "$hand Wrist to $($tip[0]) is $length m, expected about $($tip[1])"
    }
    $knuckles = Distance $bones["Lower.001"].world["ZYX"][1] $bones["Lower.004"].world["ZYX"][1]
    Write-Host ("Index to little finger base joints: {0:N4} m apart" -f $knuckles)

    # --- 3. no finger spirals ----------------------------------------------------------------------------------
    foreach ($finger in $Fingers.Keys) {
        $names = $Fingers[$finger]
        $path = 0.0
        for ($i = 1; $i -lt $names.Count; $i++) { $path += Distance $bones[$names[$i - 1]].world["ZYX"][1] $bones[$names[$i]].world["ZYX"][1] }
        $straight = Distance $bones[$names[0]].world["ZYX"][1] $bones[$names[-1]].world["ZYX"][1]
        Write-Host ("{0,-6} finger: base bone to {1} is {2:N4} m along a chain of {3:N4} (ratio {4:N3})" -f $finger, $names[-1], $straight, $path, ($straight / $path))
        Check (($straight / $path) -ge 0.85) "the $hand $finger finger spirals: its chain is $path long but its ends are only $straight apart"
    }

    # --- 4. the rotations are small bends, no 90 degree pile-ups ---------------------------------------------
    foreach ($name in $bones.Keys) {
        $deg = $bones[$name].deg
        $largest = ($deg | ForEach-Object { [Math]::Abs($_) } | Measure-Object -Maximum).Maximum
        $limit = if ($name -eq "Thumb") { 120 } else { 60 }   # the thumb base is turned about 106 degrees about Y by the glove's modelling
        Check ($largest -le $limit) "$hand bone '$name' has a local rotation of $largest degrees (limit $limit)"
    }
}

# --- 5. the right file is the left file mirrored ---------------------------------------------------------------
$left = $allBones["left"]; $right = $allBones["right"]
foreach ($name in $left.Keys) {
    if (-not $right.Contains($name)) { $failures.Add("the right model has no bone '$name'"); continue }
    $l = $left[$name]; $r = $right[$name]
    Check ($l.parent -eq $r.parent) "bone '$name' has parent '$($r.parent)' in the right model and '$($l.parent)' in the left one"
    $mirroredPosition = @(-$l.world["ZYX"][1][0], $l.world["ZYX"][1][1], $l.world["ZYX"][1][2])
    Check ((Distance $mirroredPosition $r.world["ZYX"][1]) -le $Tolerance) "bone '$name' of the right model is not the left one mirrored in X"
    # the mirror D R D: Euler X stays, Y and Z change sign
    $expectedDeg = @($l.deg[0], - $l.deg[1], - $l.deg[2])
    for ($axis = 0; $axis -lt 3; $axis++) {
        Check ([Math]::Abs($expectedDeg[$axis] - $r.deg[$axis]) -le 0.01) ("bone '$name' rotation of the right model is ({0:N3} {1:N3} {2:N3}), the mirror of the left one is ({3:N3} {4:N3} {5:N3})" -f $r.deg[0], $r.deg[1], $r.deg[2], $expectedDeg[0], $expectedDeg[1], $expectedDeg[2])
    }
}
Write-Host "`nThe right model's bones are the left model's, mirrored in X, with the same names and parents."

# --- 6. optional: where a full curl puts the fingertips ----------------------------------------------------------
if ($Curl) {
    $lua = Get-Content -Raw $Source
    $degrees = [regex]::Match($lua, "FINGER_CURL_DEGREES\s*=\s*\{\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\}")
    if (-not $degrees.Success) { throw "could not read the curl degrees from $Source" }
    $curlDegrees = @([double]$degrees.Groups[1].Value, [double]$degrees.Groups[2].Value, [double]$degrees.Groups[3].Value)
    # one direction per hand: FINGER_CURL_DIRECTION = { left = -1, right = -1 } (a single number applies to both)
    $directions = @{}
    $table = [regex]::Match($lua, "FINGER_CURL_DIRECTION\s*=\s*\{\s*left\s*=\s*(-?\d+)\s*,\s*right\s*=\s*(-?\d+)")
    $single = [regex]::Match($lua, "FINGER_CURL_DIRECTION\s*=\s*(-?\d+)")
    if ($table.Success) { $directions["left"] = [double]$table.Groups[1].Value; $directions["right"] = [double]$table.Groups[2].Value }
    elseif ($single.Success) { $directions["left"] = [double]$single.Groups[1].Value; $directions["right"] = [double]$single.Groups[1].Value }
    else { throw "could not read FINGER_CURL_DIRECTION from $Source" }
    $palmPlane = 0.17 * $MetresPerUnit
    foreach ($hand in $models.Keys) {
        $bones = $allBones[$hand]
        $sign = $directions[$hand]
        Write-Host ("`n{0} hand, full curl: joints turned by {1} * {2} degrees about their local X on top of the rest pose. The palm faces -Y, its plane is {3:N4} m in front of the knuckle line." -f $hand, $sign, ($curlDegrees -join ", "), $palmPlane)
        foreach ($finger in $Fingers.Keys) {
            $names = $Fingers[$finger]
            $knuckle = $bones[$names[1]].world["ZYX"][1]
            $tips = @{}
            foreach ($amount in 0.0, 0.2, 1.0) {
                $parentWorld = $bones[$names[0]].world["ZYX"]
                $rotation = $parentWorld[0]; $position = $parentWorld[1]
                for ($i = 1; $i -le 3; $i++) {
                    $bone = $bones[$names[$i]]
                    $offset = Apply $rotation $bone.t
                    $position = @(($position[0] + $offset[0]), ($position[1] + $offset[1]), ($position[2] + $offset[2]))
                    $rotation = Mul (Mul $rotation (Euler $bone.deg "ZYX")) (RotX ($sign * $amount * $curlDegrees[$i - 1] * [Math]::PI / 180))
                }
                $end = Apply $rotation @(0, 0, (-0.25 * $MetresPerUnit))   # the finger's end, 0.25 units along the last segment
                $tips[$amount] = @(($position[0] + $end[0]), ($position[1] + $end[1]), ($position[2] + $end[2]))
            }
            $full = $tips[1.0]
            Write-Host ("{0,-6} tip end at rest {1:N4} m in front of the knuckle line; at full curl {2:N4} in front and {3:N4} behind it toward the wrist (palm plane: {4:N4} in front)" -f `
                $finger, (- ($tips[0.0][1] - $knuckle[1])), (- ($full[1] - $knuckle[1])), ($full[2] - $knuckle[2]), $palmPlane)
            # a fifth of a curl is a small bend: the tip must move toward the palm (-Y) and toward the wrist (+Z)
            Check (($tips[0.2][1] - $tips[0.0][1]) -lt -0.0005 -and ($tips[0.2][2] - $tips[0.0][2]) -gt 0.0) "a curl does not fold the $hand $finger finger toward the palm (-Y): flip the curl direction of that hand"        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "`nFAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
Write-Host "`nOK: both models' bones compose to the armature's rest pose, in metres, and the right is the left mirrored." -ForegroundColor Green
