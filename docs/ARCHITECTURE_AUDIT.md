# Aeris — Auditoría Técnica y Arquitectura Inicial (Fase 0)

**Proyecto:** Aeris  
**Autor:** ByCesarDev  
**Versión Objetivo Confirmada:** Minecraft Bedrock **1.26.51.01** (GDK / Windows x64) + **LeviLamina Client SDK 26.51.5**  
**Fecha:** Octubre 2026  
**Referencia Base:** `RenderDragonShaderLoader` (honeymirror) + Arquitectura de UI de `Lamium` (amatouhake)

---

## 1. Contexto y Descubrimiento Crítico de Versión

Durante la auditoría inicial de las dependencias y la inspección del entorno del usuario:
- **RenderDragonShaderLoader** fue diseñado originalmente para `Minecraft 1.26.40` y dependía de `levilamina 26.40.*`.
- **La instancia real en juego es Minecraft 1.26.51.01**, y el SDK local instalado en `.xmake/packages/` es **`levilamina-client-sdk 26.51.5`**.
- **Impacto directo:** Las firmas binarias (byte signatures) y los IDs de opciones gráficas internas (`OptionID 711 / 798`) de RenderDragonShaderLoader deben tratarse como **sensibles a la versión**. No podemos asumir ciegamente que las direcciones y offsets de 1.26.20/1.26.40 coincidan exactamente en 1.26.51.01 sin un verificador seguro en tiempo de ejecución (`CompatibilityManager`).

---

## 2. Análisis Componente por Componente (RenderDragonShaderLoader)

### 2.1. Gestión de Paquetes (`src/core/ShaderPack.*`)
- **Estado:** Altamente funcional y reutilizable.
- **Qué hace:**
  - Escanea `data/shaderpacks/` buscando archivos `.zip` o carpetas.
  - Extrae `.zip` usando `tar.exe` integrado en Windows 10/11 hacia `data/generated/<pack>/src/`.
  - Soporta subpacks (ejemplo: selección de variantes con nubes volumétricas `vc` vs `novc` en packs como Mirai).
  - Parser de `shaders.properties` tipo Iris/OptiFine (sliders, toggles booleanos, enums, screens).
- **Riesgos y Mejoras para Aeris:**
  - La extracción actual usa llamadas de sistema `runHidden` con `tar.exe`. Es necesario sanitizar nombres de archivo para prevenir **Path Traversal (`../`)** o nombres maliciosos.
  - No soporta `.mcpack` directamente; en Aeris añadiremos soporte nativo para inspeccionar y descomprimir `.mcpack` sin requerir que el usuario lo renombre a mano.
  - Los parámetros modificados se guardaban en `.txt` pero no afectaban el render si no había recompilación. Aeris debe distinguir claramente qué opciones tienen variantes compiladas y cuáles son persistencia de configuración.

### 2.2. Inyecciones de Materiales y Hot-Reload (`src/hook/MaterialHooks.*`)
- **Estado:** Experimental / Versión-dependiente.
- **Mecanismo:**
  - **Hook `readFile`:** Intercepta cuando el motor Bedrock lee archivos `data/renderer/materials/*.material.bin` y los reemplaza en memoria con los binarios del pack activo.
  - **Hook `endFrame` + `reloadMaterial`:** Hookea `BgfxFrameBuilder::endFrame` y llama a `reloadMaterial` para forzar la recarga de materiales en caliente.
  - **Patrón de firmas:**
    ```cpp
    // 1.26.30 / 1.26.40 pattern
    "55 41 57 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 41 0F 10 00"
    ```
- **Riesgos en 1.26.51.01:**
  - Estas firmas deben validarse con `ll::memory::Signature::parse().view().resolve(true)`. Si en 1.26.51.01 la firma cambia o falla, el mod **nunca debe crashear**: debe registrar una advertencia clara y degradar grácilmente a modo sin hot-reload o solicitar reinicio.
  - Implementar rollback inmediato (`restoreVanilla()`) que desactive la redirección y recargue los shaders vainilla en caso de error.

### 2.3. Controlador de Modo Gráfico (`src/core/GraphicsModeController.*`)
- **Estado:** Funcional pero con IDs mágicos dependientes de versión.
- **Mecanismo:**
  - Detecta si los binarios son Deferred (`*Deferred*.material.bin`) o Forward.
  - Modifica directamente las opciones internas de Bedrock:
    - `kGraphicsModeOptionId = 711` (`graphics_mode`)
    - `kGraphicsModeSwitchOptionId = 798` (`graphics_mode_switch`)
    - Interviene `IAdvancedGraphicsOptions::setCanSwitchGraphicsModeInGame(true)`.
- **Riesgos en 1.26.51.01:**
  - En Bedrock 1.26.51.01, los `OptionID` 711 y 798 podrían haberse desplazado si Mojang añadió opciones nuevas. Aeris debe verificar el `saveTag` ("graphics_mode" / "graphics_mode_switch") antes de castear y modificar, protegiendo contra corrupciones de memoria.

### 2.4. Interfaz Gráfica (`src/ui/`)
- **Estado:** Excelente arquitectura desacoplada, inspirada en Lamium e Iris.
- **Diseño:**
  - `UiCanvas.h` define una interfaz de renderizado abstracta (fill, text, flush).
  - `UiScreen`, `UiLayout`, `UiSettings` son puras (no incluyen headers de Minecraft).
  - `ShaderManagerScreen` adopta la técnica de Lamium: abre un diálogo nativo vacío de Bedrock para ganar foco del ratón, oculta el HUD y cancela el render nativo en `BeforeUIRenderEvent` para dibujar la UI personalizada sobre `MinecraftUIRenderContext`.
- **Mejoras para Aeris:**
  - Localización bilingüe completa: **Inglés (`en_US`) y Español (`es_ES`)**. Eliminar por completo el chino (`zh_CN`).
  - Navegación mejorada con pestañas claras: *Shaders*, *Gráficos*, *Rendimiento*, *Configuración*, *Acerca de*.
  - Indicadores visuales de estado y compatibilidad.

### 2.5. Drag & Drop de Windows (`src/hook/DragDropHooks.*`)
- **Estado:** Funcional.
- **Mecanismo:**
  - Subclasea el `WNDPROC` de la ventana de Minecraft capturando `WM_DROPFILES`.
  - Copia de manera segura el archivo a `data/shaderpacks/` y emite un evento de recarga al cliente.
- **Riesgos:**
  - Manejo cuidadoso de rutas con caracteres no ASCII (UTF-8 / UTF-16 con `MoveFileExW`).

---

## 3. Obligaciones de Licencia y Proveniencia

- **RenderDragonShaderLoader:** Licenciado bajo **LGPL-3.0**.
- **Lamium:** Licenciado bajo **LGPL-3.0** (referencia técnica para UI y gestión de inputs).
- **Iris Shaders:** Licenciado bajo **LGPL-3.0** (inspiración visual y conceptual de sliders y parámetros).
- **Decisión para Aeris:**
  - Aeris se licenciará bajo **LGPL-3.0**.
  - Se mantendrá un archivo `THIRD_PARTY_NOTICES.md` detallando las contribuciones conceptuales y de código de cada proyecto.
  - Cero código de proyectos propietarios como OptiFine.

---

## 4. Estrategia de Implementación para Aeris 0.1.0-alpha

1. **Scaffolding Limpio:**
   - Crear el árbol modular en `Aeris/`.
   - `xmake.lua` configurado directamente con `levilamina-client-sdk 26.51.5` y el compilador del sistema (`clang-cl` LLVM 23.1).
2. **Capa de Compatibilidad (`CompatibilityManager`):**
   - Abstraer la resolución de firmas para `1.26.51.01`. Si la firma no resuelve, registrar un log claro en lugar de provocar un Access Violation.
3. **Internacionalización Nativa:**
   - `lang/en_US.json` y `lang/es_ES.json` integrados desde el primer momento con `LocalizationManager`.
4. **Protección en Manejo de Archivos:**
   - Sanitización de rutas antes de descompresión para prevenir vulnerabilidades.
5. **Estructura Preparada para Rendimiento:**
   - `src/optimization/PerformanceManager.h` como base modular extensible para futuras fases inspiradas en Sodium.


---

## 5. Correcciones Aplicadas en la Revisión Inicial de Base (Post-Commit d0b4229)

Tras la primera revisión técnica de la base de Aeris, se aplicaron mejoras y salvaguardas arquitectónicas esenciales:

1. **CompatibilityManager (Detección Real y Fallback Seguro):**
   - Eliminados los valores optimistas predeterminados (`mDetectedVersion = "1.26.51.01"` y `mGraphicsSwitchSupported = true`).
   - El estado inicial arranca estrictamente en `Unknown` y con todas las capacidades en `false`.
   - Se realiza la detección en tiempo de ejecución mediante `ll::getGameVersion()` y `ll::getLoaderVersion()`.
   - La compatibilidad solo se activa si la versión detectada coincide con `1.26.51.01`.

2. **PerformanceManager (Eliminación de Métricas Simuladas):**
   - Eliminada la inicialización ficticia de `60.0f` FPS.
   - El sistema opera en estado `std::nullopt` (`hasMeasurement() == false`) hasta que se procese y acumule una ventana real de al menos 1 segundo de fotogramas del juego.

3. **ConfigManager (Transaccionalidad y Reemplazo Atómico en Windows):**
   - Implementada carga transaccional: se parsea en un objeto candidato y se valida exhaustivamente (versión de esquema, protección contra *path traversal* en carpetas, validación de idioma `es_ES`/`en_US`, y rangos válidos de backdrop). Si el JSON tiene errores, la configuración anterior permanece 100% intacta.
   - En Windows, la persistencia utiliza `MoveFileExW` con `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` y reintentos, evitando problemas de bloqueo o escrituras parciales.

4. **Internacionalización Integral de Comandos:**
   - Se añadieron todas las claves de traducción de los subcomandos de `/aeris` tanto a `lang/en_US.json` como a `lang/es_ES.json`.
   - `LocalizationManager` ahora soporta sustitución posicional de parámetros (`{0}`, `{1}`) vía `format()` y `trFormat()`. Cero texto en inglés hardcodeado en la lógica de comandos.

5. **Ciclo de Vida de LeviLamina:**
   - La suscripción a `ClientCommandRegisterEvent` se gestiona con `ll::event::ListenerPtr` y se libera explícitamente en `AerisMod::disable()`.

6. **Suite de Pruebas Unitarias (AerisTests):**
   - Implementada suite en `tests/` (`TestRunner.h`, `ConfigTests.cpp`, `CompatibilityTests.cpp`, `LocalizationTests.cpp`, `PerformanceTests.cpp`, `Main.cpp`).
   - Integrada en `xmake.lua` bajo el target `AerisTests`.
   - **Resultado:** 39 pruebas ejecutadas, 39 superadas, 0 fallidas.
