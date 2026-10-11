# Aeris — Investigación Técnica de Integración RenderDragon (Fase 4)

**Proyecto:** Aeris  
**Autor:** ByCesarDev  
**Versión Objetivo:** Minecraft Bedrock **1.26.51.01** (GDK / Windows x64)  
**Framework de Modding:** LeviLamina Client SDK **26.51.5**  
**Fecha:** Octubre 2026  

---

## 1. Resumen Ejecutivo y Objetivo

El objetivo de la Fase 4 es dotar a Aeris de la capacidad de sustituir materiales gráficos compilados (`.material.bin`) utilizados por el motor RenderDragon de Minecraft Bedrock por aquellos provistos por un shaderpack activo, garantizando en todo momento la integridad del proceso de renderizado y la reversión inmediata al pipeline original de Minecraft (*fail-open to vanilla*).

A diferencia de proyectos previos como BetterRenderDragon (BRD) o RenderDragonShaderLoader (RDSL), que aplicaban ganchos en memoria (*detours*) asumiendo estabilidad en firmas binarias de versiones anteriores (1.26.20 / 1.26.30 / 1.26.40), **Aeris establece como principio arquitectónico no modificar memoria de juego a menos que el punto de integración esté explícitamente verificado para 1.26.51.01**.

---

## 2. Análisis Técnico de RenderDragonShaderLoader (`MaterialHooks.cpp`)

En la implementación de referencia (`RenderDragonShaderLoader/src/hook/MaterialHooks.cpp`), la sustitución de materiales se basa en tres componentes:

### 2.1. Hook de Lectura de Archivos (`readFile`)
- **Firma de función C++:**
  ```cpp
  using ReadFileFn = std::string* (*)(void* self, std::string* result, Core::PathView* path);
  ```
- **Mecanismo:**
  Intercepta la función interna del juego encargada de leer archivos del paquete de recursos del cliente. Cuando `path->getUtf8StringView()` contiene la subcadena `"data/renderer/materials/"` y finaliza en `".material.bin"`, el detour sustituye el contenido del buffer `result` por los bytes del archivo equivalente ubicado en el directorio de shaderpacks del mod.
- **Patrones binarios heredados (1.26.20 / 1.26.30):**
  - *1.26.30:* `55 41 57 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 41 0F 10 00`
  - *1.26.20:* `55 56 48 83 EC ? 48 8D 6C 24 ? 48 C7 45 ? ? ? ? ? 48 89 D6 48 8D 4D ? 4C 89 45 ? 4C 89 C2 E8 ...`
- **Riesgos y Limitaciones en 1.26.51.01:**
  1. **Sensibilidad del prólogo:** La secuencia `55 41 57 ... 48 81 EC` depende del tamaño exacto del marco de pila asignado por MSVC (`sub rsp, imm32`). Entre 1.26.40 y 1.26.51.01, cualquier cambio en variables locales o inlining altera la firma.
  2. **Colisiones y falsos positivos:** Un escaneo de AOB (*Array of Bytes*) que no verifique la unicidad del patrón en el binario puede resolver una función no relacionada, provocando un fallo de memoria (`STATUS_ACCESS_VIOLATION` / `0xC0000005`) al intentar invocar el puntero original.
  3. **Concurrencia:** La carga de materiales durante la inicialización o cambio de dimensión ocurre en hilos secundarios (*worker threads*). El detour debe ser estrictamente reentrante, libre de carreras de datos (*data races*) y no depender de memoria volátil.

---

### 2.2. Hook de Fin de Fotograma (`endFrame`)
- **Firma de función C++:**
  ```cpp
  using EndFrameFn = void (*)(uintptr_t self, uintptr_t ctx);
  ```
- **Mecanismo:**
  Intercepta `mce::framebuilder::BgfxFrameBuilder::endFrame`. Al finalizar la composición del fotograma en el hilo de render, si existe una solicitud de recarga en cola, invoca `reloadMaterial(self)` antes de llamar al `endFrame` original.
- **Patrones binarios heredados:**
  - *1.26.30:* `55 41 57 41 56 41 55 41 54 56 57 53 B8 ? ? ? ? E8 ? ? ? ? 48 29 C4 48 8D AC 24 ? ? ? ? 44 0F 29 8D ...`
- **Riesgos y Limitaciones en 1.26.51.01:**
  - Hookear el ciclo de dibujado de cada fotograma (60-144+ veces por segundo) añade sobrecarga de sincronización de hilos si se usa bloqueo con mutex pesados.
  - Si el puntero `self` no corresponde a la instancia esperada de `BgfxFrameBuilder`, pasarla a `reloadMaterial` corrompe la vtable o los registros de estado gráfico.

---

### 2.3. Función Objetivo de Recarga en Caliente (`reloadMaterial`)
- **Firma de función C++:**
  ```cpp
  using ReloadMaterialFn = void (*)(uintptr_t self);
  ```
- **Mecanismo:**
  Función interna de RenderDragon que purga la caché de materiales compilados y fuerza su recarga desde disco sin necesidad de reiniciar el cliente de Minecraft.
- **Patrones binarios heredados:**
  - *1.26.20:* `C6 81 ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ...`
- **Riesgos y Limitaciones en 1.26.51.01:**
  - Depende críticamente de un offset de byte anterior a la función (`C6 81 ... C3 CC`). Es sumamente frágil ante cambios menores del optimizador de enlace (LTCG / PGO) en actualizaciones de Bedrock.

---

## 3. Modelo de Capacidades de Aeris (`CapabilityState`)

Para desacoplar el estado de compilación del estado de compatibilidad real en el entorno del usuario, Aeris define el enum tri-estado:

```cpp
enum class CapabilityState {
    Unknown,     // No evaluado o pendiente de verificación en runtime
    Supported,   // Punto de integración validado exitosamente
    Unsupported  // Punto de integración no disponible, firma no encontrada o bloqueado por seguridad
};
```

### Tabla de Capacidades de Aeris en 1.26.51.01:

| Capacidad | Función Asociada | Estado Inicial | Motivo y Política de Seguridad |
|---|---|---|---|
| **MaterialRedirection** | Detour de `readFile` para sustituir `.material.bin` | `Unknown` / `Unsupported` | Bloqueado por defecto en inicio. Requiere verificación de firma en runtime. Si no resuelve de forma unívoca, permanece desactivado y el juego funciona 100% vainilla. |
| **MaterialHotReload** | Detour de `endFrame` + llamada a `reloadMaterial` | `Unknown` / `Unsupported` | Requiere que tanto `endFrame` como `reloadMaterial` estén comprobados. Si falla, el cambio de shaders requiere reiniciar el juego. |
| **GraphicsModeSwitch** | Cambio de pipeline Forward/Deferred (`OptionID 711/798`) | `Unknown` | Los IDs de opción de Bedrock pueden desplazarse. Debe verificarse el `saveTag` antes de escribir en memoria. |

---

## 4. Arquitectura de Desacoplamiento: `MaterialLoader` vs `MaterialHooks`

Para cumplir con la regla arquitectónica fundamental de **separación estricta entre lógica pura y pegamento nativo**:

1. **`MaterialLoader` (Puro, en `src/shaders/`):**
   - No incluye ningún encabezado de Minecraft ni LeviLamina.
   - Es 100% comprobable mediante pruebas unitarias (`AerisTests`).
   - Normaliza cualquier solicitud de ruta (soporta `/`, `\`, prefijos `data/renderer/materials/`, etc.).
   - Aplica validación estricta de rutas (prevención de Path Traversal `../`).
   - Valida existencia, tipo regular de archivo y límite de tamaño (`kMaxMaterialSizeBytes = 64 MB`).
   - Prioriza subpacks según el orden declarado en `manifest.json`.
   - Realiza fallback automático a vanilla ante cualquier discrepancia.
   - Ofrece `restoreVanilla()` seguro y atómico.

2. **`MaterialHooks` (Nativo, en `src/hooks/`):**
   - Utiliza las APIs de hook de LeviLamina (`ll::memory::hook`, `ll::memory::unhook`).
   - Solo se instala si `CompatibilityManager` autoriza la capacidad.
   - En `readFileDetour`, consulta a `MaterialLoader::getInstance().resolveMaterial(path)`.
   - Si `MaterialLoader` devuelve bytes, sustituye el buffer del juego. Si devuelve `std::nullopt`, deja que el motor original maneje el archivo sin alteración alguna.
   - Incluye flag atómico de bypass para desconexión instantánea sin necesidad de desenganchar en caliente si no es seguro.

---

## 5. Estrategia de Diagnóstico y Telemetría

- **Registro en carga:** Registro informativo de la versión detectada de Bedrock y el SDK de LeviLamina.
- **Registro en búsqueda de firmas:**
  - Si la firma resuelve: `info("MaterialHooks: Target found at {:#x}")`.
  - Si la firma falla: `warn("MaterialHooks: Signature not found for Minecraft 1.26.51.01; material redirection safely disabled")`.
- **Registro de resolución de materiales:**
  - Se registra de manera controlada (primeros 5 reemplazos) para evitar saturación de I/O en la consola durante el streaming del mundo.
- **Comando `/aeris info`:**
  - Informa al usuario en juego del estado exacto de cada capacidad (`Supported`, `Unsupported` o `Unknown`) y la razón técnica asociada.
