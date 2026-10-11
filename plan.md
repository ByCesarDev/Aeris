
Bro, ahora sí. 🔥 Vamos a darle a Antigravity un prompt que no se limite a decirle «crea un Iris para Bedrock», sino que le indique exactamente qué investigar, qué reutilizar, qué desarrollar y cómo comenzar a construir Aeris.

La idea es que Antigravity trabaje sobre el repositorio de RenderDragonShaderLoader como referencia, pero que Aeris sea un proyecto independiente, con arquitectura propia, interfaz en inglés y español, y una base preparada para incorporar optimizaciones similares a Sodium en el futuro.

Te recomiendo usar este prompt en modo de agente, con acceso al repositorio original y a una carpeta nueva para Aeris.

# AERIS — Master Development Prompt

## 1. Project Overview

You are the lead software architect and C++ developer responsible for creating **Aeris**, a new open-source Minecraft Bedrock Edition client mod focused on advanced shader management, RenderDragon integration, and future rendering performance optimizations.

Aeris is inspired by the user experience of Iris Shaders and the performance philosophy of Sodium, but it must be designed specifically for Minecraft Bedrock's architecture.

**Project name:** Aeris
**Author:** ByCesarDev
**Initial platform:** Windows x64
**Language:** C++20
**Modding framework:** LeviLamina
**Rendering engine:** RenderDragon
**UI languages:** English (`en_US`) and Spanish (`es_ES`)
**Initial release target:** 0.1.0-alpha
**Repository name:** Aeris

### Important references

- LeviLamina: [https://levimc.org/](https://levimc.org/)
- LeviLamina documentation: [https://lamina.levimc.org/](https://lamina.levimc.org/)
- First mod tutorial: [https://lamina.levimc.org/developer_guides/tutorials/create_your_first_mod/](https://lamina.levimc.org/developer_guides/tutorials/create_your_first_mod/)
- RenderDragonShaderLoader: [https://github.com/honeymirror/RenderDragonShaderLoader](https://github.com/honeymirror/RenderDragonShaderLoader)
- RenderDragonShaderLoader documentation: [https://github.com/honeymirror/RenderDragonShaderLoader/blob/main/README_EN.md](https://github.com/honeymirror/RenderDragonShaderLoader/blob/main/README_EN.md)
- Material hooks: [https://github.com/honeymirror/RenderDragonShaderLoader/blob/main/src/hook/MaterialHooks.cpp](https://github.com/honeymirror/RenderDragonShaderLoader/blob/main/src/hook/MaterialHooks.cpp)

## 2. Primary Objective

Create a working, maintainable and extensible foundation for Aeris.

The first milestone is **a functional shader manager for Minecraft Bedrock**, not a complete Sodium replacement.

The initial version must prioritize:

1. Reliable integration with LeviLamina.
2. Detection and management of RenderDragon shader packs.
3. Loading precompiled `.material.bin` materials when supported.
4. An integrated shader configuration interface.
5. English and Spanish localization.
6. Safe activation, deactivation and rollback.
7. Clear compatibility checks.
8. Logging, diagnostics and recoverable error handling.
9. A modular foundation for future graphics optimizations.

Do not claim that a feature works until it has been compiled and validated through appropriate tests.

---

# PHASE 0 — Repository Investigation and Technical Audit

Before writing the implementation, inspect the RenderDragonShaderLoader repository in detail.

Study at least:

- `src/core/ShaderPack.cpp`
- `src/core/ShaderPack.h`
- `src/core/GraphicsModeController.cpp`
- `src/hook/MaterialHooks.cpp`
- `src/hook/DragDropHooks.cpp`
- `src/mod/RenderDragonShaderLoader.cpp`
- `src/ui/ShaderManagerScreen.cpp`
- `src/ui/UiCanvas.h`
- `src/ui/UiLayout.cpp`
- `src/ui/UiSettings.cpp`
- `src/ui/UiScreen.cpp`
- `docs/shader-pack-format.md`
- `xmake.lua`
- `tooth.json`
- Existing configuration and localization files.

Identify which components are functional, incomplete, experimental or version-dependent.

Investigate the actual LeviLamina API and the current compatible Minecraft Bedrock versions.

Do not assume that the upstream project's 1.26.40 signatures, internal option IDs or rendering hooks work on newer Minecraft releases.

### Required audit output

Create `docs/ARCHITECTURE_AUDIT.md` containing:

- Confirmed functionality.
- Incomplete functionality.
- Version-specific dependencies.
- Binary signatures and internal hooks.
- Reusable components.
- Required rewrites.
- Licensing obligations.
- Known risks.
- Recommended implementation strategy.

Do not copy the entire upstream project blindly.

Reuse and adapt existing open-source implementations when technically appropriate, preserving copyright notices, license requirements and attribution.

If LGPL-3.0-covered code is incorporated, retain applicable notices and license obligations. Document the exact provenance of reused files.

---

# PHASE 1 — Create the Aeris Project

Create a clean, independent project called `Aeris`.

Use a modular architecture similar to the following:

```
Aeris/
├── src/
│   ├── core/
│   │   ├── AerisCore.h
│   │   ├── AerisCore.cpp
│   │   ├── ConfigManager.h
│   │   ├── ConfigManager.cpp
│   │   ├── CompatibilityManager.h
│   │   └── CompatibilityManager.cpp
│   │
│   ├── shaders/
│   │   ├── ShaderPackManager.h
│   │   ├── ShaderPackManager.cpp
│   │   ├── ShaderPackParser.h
│   │   ├── ShaderPackParser.cpp
│   │   ├── MaterialLoader.h
│   │   ├── MaterialLoader.cpp
│   │   ├── ShaderSettings.h
│   │   └── ShaderSettings.cpp
│   │
│   ├── hooks/
│   │   ├── MaterialHooks.h
│   │   ├── MaterialHooks.cpp
│   │   ├── RenderHooks.h
│   │   └── RenderHooks.cpp
│   │
│   ├── rendering/
│   │   ├── GraphicsModeController.h
│   │   ├── GraphicsModeController.cpp
│   │   ├── RenderDiagnostics.h
│   │   └── RenderDiagnostics.cpp
│   │
│   ├── optimization/
│   │   ├── PerformanceManager.h
│   │   └── PerformanceManager.cpp
│   │
│   ├── ui/
│   │   ├── AerisScreen.h
│   │   ├── AerisScreen.cpp
│   │   ├── ShaderPackScreen.h
│   │   ├── ShaderSettingsScreen.h
│   │   └── PerformanceScreen.h
│   │
│   ├── localization/
│   │   ├── LocalizationManager.h
│   │   └── LocalizationManager.cpp
│   │
│   └── mod/
│       └── AerisMod.cpp
│
├── lang/
│   ├── en_US.json
│   └── es_ES.json
│
├── config/
│   └── config.json
│
├── docs/
│   ├── ARCHITECTURE_AUDIT.md
│   ├── ARCHITECTURE.md
│   ├── COMPATIBILITY.md
│   ├── SHADER_FORMAT.md
│   └── ROADMAP.md
│
├── tests/
├── samples/
├── xmake.lua
├── tooth.json
├── README.md
├── README_ES.md
├── LICENSE
└── THIRD_PARTY_NOTICES.md
```

This is a proposed structure, not a requirement to create empty placeholder files. Adapt it when the existing code and build system justify a better design.

Use namespaces consistently, preferably `aeris`.

Keep platform-specific hooks isolated from portable shader parsing, configuration and UI logic.

---

# PHASE 2 — LeviLamina Integration

Implement the correct lifecycle for a LeviLamina client mod.

Requirements:

- Initialize Aeris when the mod loads.
- Load and validate its configuration.
- Detect the Minecraft version.
- Initialize supported rendering hooks.
- Register commands and keyboard shortcuts.
- Initialize the shader manager.
- Initialize localization.
- Cleanly release resources when supported.
- Prevent repeated initialization.
- Handle unsupported game versions safely.

Use official LeviLamina APIs whenever possible.

Do not invent API methods, events or symbols.

Check the installed LeviLamina version and documentation before implementing any integration.

### Compatibility

Create a compatibility layer that associates supported Minecraft versions with:

- Required internal symbols.
- Binary signatures when necessary.
- Rendering hook capabilities.
- Graphics option mappings.
- Tested compatibility status.

Validate resolved signatures and function targets before activating a hook.

If validation fails, disable the affected functionality and display a meaningful error instead of attempting an unsafe hook.

Do not scan for arbitrary alternative signatures and invoke unknown functions.

Do not modify the Minecraft executable on disk.

---

# PHASE 3 — Shader Pack Management

Implement a complete shader pack management foundation.

### Supported input

Initially support:

- `.zip` shader packs.
- Extracted shader pack directories.
- `.mcpack` archives when their contents follow a compatible structure.
- Precompiled `.material.bin` files.

Do not treat arbitrary Java Edition Iris/OptiFine shaderpacks as compatible with RenderDragon.

Inspect archive contents to identify whether a pack is usable.

### Pack metadata

Each discovered pack should expose:

- Display name.
- Author.
- Description.
- Version.
- Pack type.
- Available variants or subpacks.
- Available settings.
- Compatibility status.
- Validation errors.

Use metadata files when available, with reasonable fallbacks.

### Installation

Implement:

- Import from a local file.
- Drag-and-drop installation on Windows.
- Automatic pack discovery.
- Pack validation.
- Pack selection.
- Activation and deactivation.
- Pack removal.
- Re-scanning.
- Persistent selection across sessions.

### Archive security

Prevent:

- Path traversal.
- Extraction outside the destination directory.
- Unsafe symbolic links.
- Archive bombs through excessive extraction sizes.
- Accidental overwriting of unrelated files.

Avoid shell-command construction using untrusted filenames.

Prefer safe archive-processing libraries or properly validated native APIs.

---

# PHASE 4 — RenderDragon Material Integration

Study the original MaterialHooks implementation carefully.

The initial implementation should support loading precompiled material binaries through verified RenderDragon hooks.

Required behavior:

1. Detect the selected shader pack.
2. Identify available material overrides.
3. Intercept supported material requests.
4. Resolve an override only when a matching material exists.
5. Fall back to the original material otherwise.
6. Track activation and reload state.
7. Support returning to vanilla materials.
8. Prevent invalid material data from being used.
9. Log meaningful failures.

### Hot reload

Investigate the original implementation involving:

- `readFile`
- `endFrame`
- `reloadMaterial`

Do not assume the hot-reload implementation is safe or functional merely because hooks exist.

Validate calling conventions, object lifetime, thread affinity and reload behavior.

If safe hot reload cannot be confirmed, implement deferred activation or require a restart rather than risking crashes.

### Graphics modes

Investigate the correct handling of Forward and Deferred materials.

Do not hardcode graphics option IDs globally.

Version-specific identifiers must live inside the compatibility layer.

Only switch graphics modes when the operation is supported and validated.

---

# PHASE 5 — Shader Settings System

Implement a configurable shader settings engine inspired by Iris.

Support parsing compatible `shaders.properties` files, where applicable.

Potential setting types:

- Boolean toggles.
- Numeric sliders.
- Enumerations.
- Color values.
- Categories.
- Nested settings pages.

Settings must be persisted per shader pack.

Provide:

- Apply.
- Cancel.
- Reset to defaults.
- Restore previous values.
- Detection of invalid settings.

### Critical requirement

Do not pretend that changing a setting modifies the shader visually if Aeris cannot rebuild or select the corresponding material.

Separate:

- Settings that affect compiled output.
- Settings that select existing material variants.
- Settings that are stored but not yet applicable.

Only expose working controls as fully functional.

---

# PHASE 6 — Shader Compilation Research

Investigate integrating Lazurite or another compatible material compilation tool.

Determine:

- Supported input formats.
- Output material formats.
- Supported RenderDragon versions.
- Compiler dependencies.
- Licensing.
- How shader parameters can be injected.
- How compiled output can be cached.
- Whether runtime compilation is practical.

Design interfaces such as:

- `IShaderCompiler`
- `ShaderCompilationJob`
- `ShaderCompilationResult`
- `ShaderCache`

Do not integrate an unverified compiler merely to claim compilation support.

If compilation is not viable during version 0.1.0-alpha, document the limitation and keep precompiled material loading as the supported workflow.

---

# PHASE 7 — Aeris User Interface

Create an integrated, lightweight and polished in-game interface.

Visual direction:

- Minimalist.
- Clean typography.
- Consistent spacing.
- Dark theme.
- Subtle animations.
- Clear status indicators.
- No unnecessary gradients.
- Inspired by the usability of Iris, without copying its visual assets.

### Main screen

The interface should contain:

**Shader Packs**

- Installed shader packs.
- Currently active pack.
- Import button.
- Activate/deactivate controls.
- Shader pack settings.
- Reload action when supported.

**Graphics**

- Current graphics mode.
- Supported rendering options.
- Compatibility information.

**Performance**

- Diagnostic information.
- Future optimization controls.

**Settings**

- Language selection.
- Keyboard shortcut.
- Logging preferences.
- Safe mode.

**About**

- Aeris version.
- Author: ByCesarDev.
- License.
- Third-party credits.

Use existing RenderDragonShaderLoader UI techniques as reference where appropriate.

Ensure mouse and keyboard interactions do not interfere with normal gameplay.

The UI must properly release cursor focus and restore normal game controls when closed.

### Default shortcut

Use `G` initially, but allow customization and handle potential conflicts.

---

# PHASE 8 — Localization

Aeris must support English and Spanish from its first version.

Create:

- `lang/en_US.json`
- `lang/es_ES.json`

English is the fallback language.

Provide a localization manager with:

- Translation key lookup.
- Missing-key fallback.
- Language switching.
- Safe handling of malformed translation files.
- UTF-8 support.
- Optional parameter substitution.

Translate every user-facing string, including:

- Buttons.
- Menus.
- Tooltips.
- Errors.
- Warnings.
- Notifications.
- Compatibility messages.
- Shader pack status.

Do not include Chinese localization in Aeris.

Do not leave hardcoded user-facing English strings scattered throughout the UI.

---

# PHASE 9 — Performance Foundation

Aeris is intended to support future Sodium-inspired optimizations.

However, version 0.1.0-alpha must not implement speculative rendering patches.

Create a small, extensible performance subsystem.

Initial responsibilities:

- Expose available diagnostic capabilities.
- Establish baseline performance measurements.
- Record compatible rendering backend information when accessible.
- Measure Aeris-specific processing overhead.
- Provide structured logging for performance experiments.

Investigate future optimization opportunities:

- Chunk mesh generation.
- Frustum and occlusion culling.
- Draw-call reduction.
- Render-state changes.
- Resource reuse.
- Shader caching.
- Frame pacing.
- Memory management.

For every proposed optimization, document:

- Internal function or pipeline stage involved.
- Evidence that the hook is accessible.
- Expected benefit.
- Potential overhead.
- Compatibility risk.
- Benchmark method.

Do not introduce optimization switches that do nothing.

Do not claim FPS improvements without reproducible measurements.

---

# PHASE 10 — Configuration and Persistence

Use a clear, versioned configuration format.

Suggested configuration:

```
{
  "configVersion": 1,
  "language": "en_US",
  "ui": {
    "shortcut": "G",
    "theme": "dark"
  },
  "shaders": {
    "enabled": false,
    "activePack": null,
    "hotReload": false
  },
  "performance": {
    "diagnosticsEnabled": false
  },
  "safety": {
    "safeMode": false
  }
}
```

Validate configuration values.

Handle missing, outdated and malformed configuration files.

Use atomic writes where practical to avoid corrupting saved settings.

Do not overwrite user preferences unnecessarily.

---

# PHASE 11 — Testing and Stability

Create meaningful tests for components that can be tested outside Minecraft.

At minimum:

- Shader pack discovery.
- Archive validation.
- Path traversal protection.
- Metadata parsing.
- Shader property parsing.
- Configuration persistence.
- Localization fallback.
- Compatibility version matching.
- Material path resolution.

Include tests for malformed inputs and missing files.

### Runtime testing

Where a compatible Minecraft installation is available, validate:

1. Aeris loads successfully.
2. The UI opens and closes.
3. Shader packs are discovered.
4. Pack installation works.
5. Precompiled materials can be activated.
6. Vanilla materials can be restored.
7. Graphics mode changes work when supported.
8. Unsupported versions fail safely.
9. English and Spanish interfaces display correctly.
10. Aeris does not introduce obvious regressions in normal gameplay.

Never report in-game tests as passed unless they were actually executed.

If the environment cannot run Minecraft, clearly identify which checks require manual in-game validation.

---

# PHASE 12 — Documentation and Packaging

Create professional documentation.

### README.md

Include:

- Project description.
- Features.
- Supported Minecraft versions.
- Requirements.
- Installation instructions.
- Usage instructions.
- Shader pack compatibility.
- Known limitations.
- Build instructions.
- License and acknowledgments.

### README_ES.md

Provide an equivalent Spanish README.

### Additional documentation

- `docs/ARCHITECTURE.md`
- `docs/COMPATIBILITY.md`
- `docs/SHADER_FORMAT.md`
- `docs/ROADMAP.md`
- `docs/BUILDING.md`

Use xmake and the appropriate LeviLamina build conventions.

Prepare the package metadata for distribution, but do not publish releases or push commits without authorization.

---

# DEVELOPMENT PRIORITIES

Implement features in this order:

**Priority 1 — Essential**

- Project setup.
- Successful compilation.
- LeviLamina initialization.
- Configuration.
- Localization.
- Shader pack discovery.
- Safe compatibility checks.

**Priority 2 — Core functionality**

- Shader pack import.
- UI integration.
- Precompiled material loading.
- Activation and rollback.
- Per-pack settings persistence.

**Priority 3 — Enhancements**

- Drag-and-drop.
- `.mcpack` support.
- Graphics mode switching.
- Verified hot reload.
- Improved diagnostics.

**Priority 4 — Future research**

- Shader compilation.
- Shader caching.
- RenderDragon optimizations.
- Performance profiling.
- Cross-version support.

Do not sacrifice stability to implement lower-priority features.

---

# DEVELOPMENT RULES

1. Build actual functionality, not mock implementations.
2. Do not generate empty placeholder classes unnecessarily.
3. Prefer small, testable modules.
4. Use RAII and modern C++ practices.
5. Avoid unsafe global state when possible.
6. Avoid unnecessary dependencies.
7. Validate pointers, buffers and version-dependent addresses.
8. Never silently swallow critical errors.
9. Keep rendering hooks isolated.
10. Preserve third-party license requirements.
11. Do not modify the original upstream repository.
12. Do not delete existing user files.
13. Do not invent benchmark results.
14. Do not report unsupported features as implemented.
15. Do not publish or push anything without permission.
16. Document any important deviation from the proposed architecture.

---

# EXECUTION INSTRUCTIONS

Start working now.

First, inspect the available repositories and development environment.

Then:

1. Complete the technical audit.
2. Determine the correct initial Minecraft and LeviLamina version combination.
3. Create the independent Aeris project.
4. Configure the build system.
5. Implement the foundational modules.
6. Implement the first functional shader-management workflow.
7. Compile and fix build errors.
8. Run available automated tests.
9. Document runtime validation requirements.
10. Summarize completed work and remaining blockers.

Work incrementally, validating each major subsystem before proceeding.

If the development environment lacks Minecraft, LeviLamina, a compiler, or other required dependencies, do not fabricate success. Implement what can be validated and explain the remaining requirements.

**Definition of done for the first milestone:**

Aeris has an independently buildable project, correct LeviLamina integration for its selected target version, bilingual configuration and UI infrastructure, working shader pack discovery, and a verified or explicitly documented material-loading integration.

Do not consider the first milestone complete if the project does not compile.

The final report must distinguish:

- Implemented and tested.
- Implemented but not runtime-tested.
- Experimental.
- Not implemented.
- Blocked.

The goal is not to create a quick clone of RenderDragonShaderLoader.

**The goal is to establish Aeris as a maintainable, independent Minecraft Bedrock rendering project with a reliable shader foundation and a technically credible path toward real performance optimization.**

## Lo que Antigravity debería entregarnos primero

Antes de comenzar a agregar funciones avanzadas, espero que produzca estos resultados:

Proyecto Aeris independiente

Estructura, configuración, dependencias y compilación inicial.

Interfaz bilingüe

Inglés y español desde el principio, sin chino.

Sistema de shaders

Descubrimiento, importación, selección y carga de materiales compatibles.

Compatibilidad y seguridad

Validación de versiones, manejo de errores y restauración de materiales originales.

Un detalle importante: aunque anteriormente hablamos de Bedrock 26.40, no quiero que Antigravity se quede atado a esa versión. El prompt le exige comprobar cuál es la versión realmente compatible con el SDK y los hooks disponibles antes de establecer el objetivo inicial.

Y tampoco le estamos pidiendo reescribir absolutamente todo desde cero: debe identificar las partes útiles de RenderDragonShaderLoader y reutilizarlas cuando convenga, manteniendo Aeris como proyecto independiente y respetando la LGPL-3.0.

Con este prompt ya puede comenzar el desarrollo real de Aeris 0.1.0-alpha.
