# Entity model format

Entity textures are pixel atlases. A model part's `uv` and `textureDimensions`
use Minecraft's unfolded cuboid layout (top/bottom followed by left, front,
right, and back). They are never repeated across the cuboid.

Parts may also provide a `faces` object when the texture uses a custom atlas.
Each named face (`left`, `right`, `front`, `back`, `top`, or `bottom`) accepts
either `[left, top, right, bottom]` pixel coordinates or an object such as:

```json
"front": { "uv": [8, 8, 16, 16], "rotation": 90 }
```

`rotation` on a model part is an Euler `[x, y, z]` rotation in degrees around
that part's `pivot`. This permits tilted cuboids and articulated multipart
models instead of requiring every part to remain axis-aligned.

For unrestricted render geometry, a part can use arbitrary vertices and
polygon faces instead of `min`/`max`. UV coordinates remain atlas pixels:

```json
{
  "vertices": [
    { "position": [0, 1, 0], "uv": [8, 0] },
    { "position": [-0.5, 0, 0.5], "uv": [0, 8] },
    { "position": [0.5, 0, 0.5], "uv": [8, 8] },
    { "position": [0, 0, -0.5], "uv": [16, 8] }
  ],
  "faces": [[0, 1, 2], [0, 2, 3], [0, 3, 1], [1, 3, 2]],
  "pivot": [0, 0.5, 0],
  "rotation": [0, 15, 0],
  "bone": 0
}
```

Faces may contain three or more indices and are triangulated at load time. Each
individual face must be planar and convex, but the complete render mesh may be
concave. Vertices can be duplicated where a UV seam needs different atlas
coordinates; shading normals are generated independently for every face.

An entity `hitbox` can retain the legacy `width`/`height` fields or contain a
compound `boxes` array. Each collision box has `min`, `max`, and optional
`pivot` and `rotation` fields. Compound boxes are tested as oriented boxes
against world blocks, including the entity's current yaw.

`hitbox.hulls` adds arbitrary convex collision polyhedra. Each hull contains a
`vertices` array and polygon `faces`, plus optional `pivot` and `rotation`.
Collision uses face normals and derived edge axes rather than reducing the hull
to a bounding box. Ray hits are tested against the actual face triangles.
Combine multiple convex hulls to describe a concave model.

## Animation timing

Movement animations can include a `timing` array inside their `animation`
object. Player walk, use, and attack timing use the same segment format in
`assets/player/animation.json`.

```json
"timing": [
  { "duration": 0.40, "speed": 1.0, "easing": "easeIn" },
  { "duration": 0.10, "speed": 0.0 },
  { "duration": 0.50, "speed": 0.8, "easing": "easeOut" }
]
```

`duration` is the segment's share of the complete animation timeline; the
values do not need to add up to one. `speed` controls how much source animation
is traversed during that segment. A speed of zero holds the current pose.
Supported easing values are `linear`, `smoothStep`, `easeIn`, `easeOut`, and
`hold`. The system normalizes travelled animation distance so a looping clip
always rejoins its first frame without a jump.

For precise authoring, `timeCurve` can replace the segment timing map. Curve
keys have `time`, `value`, optional `inTangent`/`outTangent`, and an
`interpolation` of `step`, `linear`, or `cubic` (cubic Hermite). Times and
values normally span 0-1. Curves also accept `pre` and `post` extrapolation of
`clamp`, `loop`, or `pingPong`:

```json
"timeCurve": {
  "pre": "clamp", "post": "clamp",
  "keys": [
    { "time": 0.0, "value": 0.0, "outTangent": 0.2, "interpolation": "cubic" },
    { "time": 0.35, "value": 0.12, "inTangent": 0.4, "outTangent": 2.0, "interpolation": "cubic" },
    { "time": 1.0, "value": 1.0, "inTangent": 0.5 }
  ]
}
```

At runtime the same curve type is usable for blend weights and parameters.
Skeletal clips support per-track step, linear, and cubic interpolation,
looping or clamped playback, time curves, quaternion rotation interpolation,
pose blending, and optional per-bone blend masks through
`core/animationSystem.h`.

## Health, hitboxes, and render layers

`maxHealth` sets the health assigned when an entity spawns. Attacks are ray
tested against `hitbox.boxes` and `hitbox.hulls`, so compound and rotated shapes
remain valid damage targets rather than being reduced to the visible model's
bounds.

An entity can add independently textured model layers with `layers`. Each layer
has its own `model`, `texture`, and `textureSize`. `hideWhenSheared: true` marks
a wool or fur shell that disappears when a `shearable` entity is sheared,
leaving the base model and its inner texture visible.
