# GUI Rounded Corners

How `cornerRadius` is implemented in the Clay GUI renderer

## The problem

Clay carries a corner radius through its layout and hands it to the renderer on the render command:

```cpp
typedef struct Clay_RectangleRenderData {
    Clay_Color backgroundColor;
    Clay_CornerRadius cornerRadius;     // topLeft, topRight, bottomLeft, bottomRight
} Clay_RectangleRenderData;
```

`Clay_ImageRenderData` has the same field. Clay does no rasterization of its own, so honoring the radius is entirely
up to `GUI::End()`. The original implementation pushed a plain 4-vertex quad per command and dropped the radius on the
floor, so `.cornerRadius = CLAY_CORNER_RADIUS(8)` in the layout code had no visible effect.

## Why CPU tessellation and not an SDF shader

The obvious GPU approach is a signed-distance rounded box in `GUIShape.hlsl`: pass the shape's half-extents and radius
down to the fragment shader, evaluate `sdRoundedBox()`, and modulate alpha. It gives cheap anti-aliasing and constant
geometry cost per shape.

It does not fit this renderer without a wider refactor. The fragment shader needs per-shape data, which means new
vertex attributes, which means growing `GUIVertex` past `sizeof(TextVertex)`. Two things break at that point:

1. The text path writes glyphs by casting the vertex batch: `TextBuilder::CreateText((TextVertex*)v, ...)`. That cast
   is only valid while the two structs are layout compatible, which is what the `static_assert` in `GUI::End()`
   guards.
2. Text and shapes share one vertex buffer, but the text pipeline's vertex input layout is built inside
   `TextBuilder::HelperCreateGraphicsObjects` with `stride = sizeof(TextVertex)`. A wider `GUIVertex` would have to be
   bound to a pipeline expecting the narrower stride.

Working around both means either separate vertex buffers per pipeline, or hand-rolling the text pipeline in the GUI
code instead of reusing the `TextBuilder` helper. Tessellating on the CPU avoids all of it: the rounding lives purely
in geometry, so the vertex format, the shaders, the pipelines and the batching are all untouched.

## The geometry

`_PushQuadRoundedCorner()` emits a triangle fan: one vertex at the box center, then a ring of vertices around the
outline. `_PushQuad()` is kept as-is and still handles the `radius == 0` case, since a 4-vertex quad is much cheaper
than a 21-vertex fan.

The ring is built from four quarter-turn arcs, one per corner, centered on the corner's inset center:

```
    (x+r, y+r)                    (x+w-r, y+r)
        +-----------------------------+
        |  TL                     TR  |
        |                             |
        |  BL                     BR  |
        +-----------------------------+
    (x+r, y+h-r)                (x+w-r, y+h-r)
```

Angles are measured from the +x axis in the framebuffer's y-down space, so `sin` grows downward. Each corner starts a
quarter turn behind the previous one and sweeps by `-M_HALFPI`:

```cpp
float startAngle = 3.0f*M_HALFPI - float(corner)*M_HALFPI;
float angle = startAngle - M_HALFPI*(float(s)/float(numSegments));
Float2 p = cornerCenters[corner] + Float2(M::Cos(angle), M::Sin(angle))*cornerRadius;
```

With the corner order top-left, bottom-left, bottom-right, top-right that produces:

| corner | center       | angle sweep | first point   | last point    |
|--------|--------------|-------------|---------------|---------------|
| TL     | (x+r, y+r)   | 270° -> 180° | (x+r, y)      | (x, y+r)      |
| BL     | (x+r, y+h-r) | 180° -> 90°  | (x, y+h-r)    | (x+r, y+h)    |
| BR     | (x+w-r,y+h-r)| 90° -> 0°    | (x+w-r, y+h)  | (x+w, y+h-r)  |
| TR     | (x+w-r, y+r) | 0° -> -90°   | (x+w, y+r)    | (x+w-r, y)    |

The straight edges need no special handling. Each arc ends where the next one begins (TL ends at `(x, y+r)`, BL starts
at `(x, y+h-r)`), so the single fan triangle spanning those two ring vertices covers the whole left edge. The ring
closes back onto the first vertex across the top edge, which the modulo in the index loop takes care of:

```cpp
for (uint32 s = 0; s < numRingVertices; s++) {
    idx[s*3 + 0] = centerIndex;
    idx[s*3 + 1] = centerIndex + 1 + s;
    idx[s*3 + 2] = centerIndex + 1 + ((s + 1) % numRingVertices);
}
```

No ring vertex is duplicated, and every triangle follows the ring direction, so the fan has the same winding as
`_PushQuad`'s `TL -> BL -> BR -> TR`. The shape pipeline runs with `GfxCullMode::None`, so winding is cosmetic here,
but keeping the two paths consistent means either can be swapped in without a culling surprise.

## Details worth remembering

**Radius clamping.** `cornerRadius` is clamped to `Min(box.width, box.height)*0.5f`. Past that the arcs would
overshoot each other and the outline would self-intersect. A 40px tall button clamps at 20.

**Segment count.** `Clamp(uint32(cornerRadius*0.5f), 2u, 8u)` — one segment per two pixels of radius, so small radii
do not pay for geometry nobody can see. At radius 8 that is 4 segments per corner: 20 ring vertices plus the center,
and 60 indices per shape.

**UVs.** Ring vertices get a UV by mapping their position within the bounding box, rather than the corner-only UVs
`_PushQuad` assigns. Rectangles sample the 1x1 white texture so it makes no difference there, but it keeps a rounded
*image* sampling correctly.

**Batching is unaffected.** Because the rounding is geometry, a rounded rect still shares a draw command with its
neighbours — same pipeline, same white texture. An SDF implementation with per-draw push constants would have forced a
batch break per rounded shape.

## Known limitations

- **No anti-aliasing.** The curves are hard-edged, consistent with every other edge this renderer produces. This is
  the main thing an SDF shader would buy, and the reason to revisit the approach if the GUI ever gets AA generally.
- **A single radius per shape.** `_CornerRadius()` collapses Clay's four per-corner radii to their maximum.
  `CLAY_CORNER_RADIUS(n)` sets all four equal so this is lossless in practice, but a genuinely per-corner radius is
  silently flattened. Supporting it means indexing `cornerCenters[corner]` against a per-corner radius array and
  clamping each independently.
- **Corner radius on text is ignored**, since text geometry comes from `TextBuilder::CreateText`.
