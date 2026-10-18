# Third-party terrain reference

`SingularityPlanetTerrainNext` adapts algorithms from DeepSpaceEngine by Les Bird.

- Upstream: https://github.com/lesbird/DeepSpaceEngine
- Inspected commit: `6c16ec7d59906d28d85f41b790ca2dfd252f1e02`
- License: MIT. See `LICENSES/DeepSpaceEngine-MIT.txt`.

R1 ports the cube-face direction map (`FacePointD`, `DirToFaceUV`) and the rule that camera-relative GPU positions subtract in double before a float cast (`UniversePosition.ToCameraRelative`). It does not copy OpenGL, GLSL, or C# source files.

The local clone used for the audit lives outside this plugin:

`C:\Users\luka\Documents\Unreal Projects\External\DeepSpaceEngine`
