#include "core/CompatibilityManager.h"

namespace aeris {

CompatibilityManager& CompatibilityManager::getInstance() {
    static CompatibilityManager instance;
    return instance;
}

void CompatibilityManager::initialize() {
    // Inicialización de la capa de compatibilidad para 1.26.51.01
    mVersionCompatible = true;
}

} // namespace aeris
