# MyDoom — Image Impostor LOD System

## Agent task
Implement a distance-based visual representation system in MyDoom that renders nearby objects using their full 3D geometry, medium-distance objects using simplified geometry, and distant objects using image-based impostors. When the player approaches an impostor, transition back to real geometry. Preserve the existing renderer and current culling architecture; do not rewrite unrelated systems.


## Goal
Reduce CPU work in MyDoom's custom software rasterizer by avoiding detailed mesh processing for objects that are far from the camera. Distant scenery should be represented by textured image planes (impostors), then replaced with real geometry as the player gets closer.

This is a Level-of-Detail (LOD) system with image-based LOD, not a system that reconstructs meshes from photographs. The source 3D model and its impostor are alternative render representations of the same object.

## Representation tiers
Implement a configurable tier system:

- **LOD 0 — Full geometry:** nearby objects use their complete 3D mesh and existing material/texture behavior.
- **LOD 1 — Simplified geometry:** medium-distance objects use a lower-detail mesh, when one is available. If no simplified mesh exists, use Full Geometry or Impostor according to a clearly documented fallback policy.
- **LOD 2 — Impostor:** far objects use an image texture on a billboard/appropriate plane instead of their full mesh.
- **Beyond render distance:** objects may be omitted or handled by the existing fog/far-distance behavior. Do not introduce a new visual cutoff without making it configurable and consistent with existing engine behavior.

Initial distance values may be exposed as settings and used as provisional defaults (for example, Full Geometry up to ~25 world units, Simplified Geometry to ~70, Impostor to ~180). These are experiment values only; do not hard-code them as final design assumptions. Confirm the engine's world scale and existing far-clip behavior first.

## Impostor types
Support a simple, extensible design rather than attempting to implement every impostor technique immediately:

1. **Single-view impostor:** one image on a billboard. Suitable for objects that are not expected to be inspected from multiple angles.
2. **Multi-view impostor (future-ready):** a set of directional images (e.g. 8 or 16 views) selected based on camera azimuth, to reduce the flat-card appearance when the camera rotates around an object.
3. **Depth/parallax impostor (future extension):** image plus depth data, optional future work; do not implement unless the existing architecture makes it straightforward and it does not delay the core feature.

For the initial implementation, prioritize Single-view impostors and structure the data/API so Multi-view can be added later without rewriting LOD selection.


## LOD selection and transition behavior
- Calculate camera-to-object distance and choose the appropriate representation tier.
- Perform existing **Chunk Frustum Culling first**, then Sector Culling where applicable, then object-level visibility/culling. Do not calculate or render LOD for objects already rejected by culling.
- Keep LOD decisions stable near tier boundaries using **hysteresis**. Example: if Full Geometry switches to Simplified at 25 units, it should not switch back until the player is closer than a separate threshold. Choose sensible thresholds and make them configurable.
- Avoid frequent allocation, loading, or expensive resource lookups inside the per-frame loop. Cache the selected tier and/or resources where practical.
- If cross-fade is supported by the renderer, it may be added as an optional transition. Do not require cross-fade if the software renderer cannot do it efficiently. A hard switch with hysteresis is acceptable for the first version.
- Ensure an impostor does not visibly pop, disappear, or flicker due to incorrect bounds or depth handling.

## Impostor rendering requirements
- Render the image as a billboard or another camera-facing representation compatible with the existing software rasterizer.
- Maintain correct approximate world size, pivot/base position, vertical placement, and depth so that it occupies the same space as the original object.
- Use the existing texture/material pipeline where possible; avoid creating a separate incompatible rendering path.
- Respect transparency/alpha if the renderer already supports it. If not, document limitations and avoid breaking existing materials.
- Ensure the impostor is not rendered in addition to its full mesh at the same time, except during an intentional transition.
- Avoid using a single flat image for objects where side-view rotation would cause severe visual artifacts; provide a documented limitation and/or allow those objects to opt out of impostors.

## Data and authoring design
Introduce a minimal LOD/impostor data definition that can associate representations with one object. It should be compatible with existing asset conventions and not require all objects to have every LOD tier.

Potential data fields (adapt to existing engine conventions rather than copying this schema blindly):
- full mesh/reference
- optional simplified mesh/reference
- optional impostor image/texture reference
- impostor dimensions or scale
- pivot/base offset
- LOD distance thresholds
- optional multi-view image list/directions (future-ready only if useful)
- flag to disable LOD or impostors for an object

Missing tiers must have a predictable fallback and must not cause crashes or blank objects.

## Integration constraints
- Review the current code and reuse existing renderers, resource-loading, world/chunk, object, and camera abstractions.
- Integrate with the existing **Chunk Frustum Culling → Sector Culling → Object Culling** pipeline. Keep culling conservative: partially intersecting bounds must remain visible.
- Preserve gameplay-critical objects, collision, physics, triggers, AI, and interactions. LOD is visual only; do not remove or replace collision/interaction representations.
- Avoid unnecessary copies of mesh/texture data and avoid per-frame heap allocations.
- Keep code portable and aligned with the current C++ standard and build setup.
- Do not add a heavyweight external dependency solely for LOD/impostor support.
- Do not modify unrelated gameplay, map generation, input, UI, or build configuration unless required for integration.

## Suggested implementation stages
1. Inspect current renderer, mesh/sprite representations, texture loading, world object definitions, Chunk/Sector/Object culling, and current far-distance settings. Report relevant files and existing patterns briefly before editing.
2. Design the smallest compatible LOD data structure and tier-selection API.
3. Implement distance-based tier selection with configurable thresholds and hysteresis.
4. Implement single-view impostor rendering through the existing software rasterizer/texture path.
5. Integrate LOD selection into the rendering loop after culling, without changing gameplay logic.
6. Add fallback behavior for missing simplified meshes or impostor images.
7. Add focused tests for distance boundaries, hysteresis, missing tiers, bounds, and culling order where the project test setup supports them. If no test framework exists, add a small standalone test only if consistent with the repository conventions; do not leave untracked scratch tests as the only validation.
8. Build the project using the existing documented build command and report the actual result. Do not claim a build or test passed unless it was run.

## Acceptance criteria
- Nearby objects are rendered with full geometry.
- Medium-range objects can use simplified geometry when supplied.
- Far objects with impostor assets render as images rather than full meshes.
- Approaching an object switches it back to real geometry at the configured threshold.
- LOD switching is stable near boundaries (hysteresis prevents rapid toggling).
- Objects without an impostor or simplified representation continue rendering correctly.
- LOD is evaluated only for objects that survive relevant culling stages.
- Visual LOD does not alter collisions, physics, gameplay, or interactions.
- Existing scenes/maps without LOD metadata remain compatible.
- No avoidable per-frame allocations or duplicate mesh/impostor drawing are introduced.
- Project compiles successfully; test/build output is included in the completion report.

## Agent completion report
When finished, provide:
1. Summary of the implemented design.
2. List of files created/modified and why.
3. Exact LOD distance thresholds and where they are configurable.
4. Fallback behavior when an LOD asset is missing.
5. Tests performed and their results.
6. Build command and actual build outcome.
7. Known limitations (e.g. single-view impostor side-angle artifacts) and recommended next steps.

Do not claim that photographs are automatically converted into 3D meshes. The feature replaces detailed distant geometry with images and restores geometry when the player approaches.
