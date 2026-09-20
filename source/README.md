# Source layout

- `main.cpp` is the process entry point.
- `app/` owns application composition and frame orchestration.
- `engine/core/` contains the window, graphics, camera, player, shader, and texture foundations.
- `engine/renderer/` contains reusable rendering systems and GPU buffers.
- `engine/world/` contains chunks, terrain generation, streaming, and GPU meshing.
- `engine/assets/` contains CPU/GPU asset loading and registries.
- `engine/ui/` contains the UI renderer and widgets.

Third-party code and prebuilt libraries live separately under `dependencies/`.

Every first-party engine header has a matching `.cpp` translation unit. Concrete
implementations should live in the `.cpp`; templates and compile-time helpers stay
in the header because their definitions must be visible to callers.
