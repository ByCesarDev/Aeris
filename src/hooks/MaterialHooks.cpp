#include "hooks/MaterialHooks.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if __has_include("ll/api/memory/Hook.h")
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Signature.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/deps/core/file/PathView.h"
#endif

#include "core/CompatibilityManager.h"
#include "core/ConfigManager.h"
#include "core/SignatureVerifier.h"
#include "shaders/MaterialLoader.h"

namespace aeris {

namespace {

#if __has_include("ll/api/memory/Hook.h")

// Candidate patterns with targeted versions
struct PatternCandidate {
    std::string_view name;
    std::string_view pattern;
    std::string_view targetVersion;
};

// 1. readFile candidates
constexpr PatternCandidate readFileCandidates[] = {
    // 1.26.30 / 1.26.40 / 1.26.51 (Confirmed unique on 1.26.51.01 at RVA 0x2cb260)
    {"1.26.30/40/51", "55 41 57 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 41 0F 10 00", "1.26.51.01"},
    // 1.26.20 (Legacy; 0 matches on 1.26.51.01)
    {"1.26.20", "55 56 48 83 EC ? 48 8D 6C 24 ? 48 C7 45 ? ? ? ? ? 48 89 D6 48 8D 4D ? 4C 89 45 ? 4C 89 C2 E8 ? ? ? ? 48 8D 55 ? 48 89 F1 E8 ? ? ? ? 48 8B 4D ? E8 ? ? ? ? 48 89 F0 48 83 C4 ? 5E 5D C3 66 66 2E 0F 1F 84 00 ? ? ? ? 48 89 54 24 ? 55 56 48 83 EC ? 48 8D 6A ? 48 8B 4D ? E8 ? ? ? ? 90 48 83 C4 ? 5E 5D C3 55 56", "1.26.20"}
};

// 2. endFrame candidates
constexpr PatternCandidate endFrameCandidates[] = {
    // 1.26.30 / 1.26.40 / 1.26.51 (Confirmed unique on 1.26.51.01 at RVA 0x5e25fc0)
    {"1.26.30/40/51", "55 41 57 41 56 41 55 41 54 56 57 53 B8 ? ? ? ? E8 ? ? ? ? 48 29 C4 48 8D AC 24 ? ? ? ? 44 0F 29 8D ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 95 ? ? ? ? 48 89 8D", "1.26.51.01"},
    // 1.26.20 (Obsolete on 1.26.51.01; false positive at RVA 0xb75dda0)
    {"1.26.20", "55 41 57 41 56 41 55 41 54 56 57 53 B8 ? ? ? ? E8 ? ? ? ? 48 29 C4 48 8D AC 24 ? ? ? ? 66 44 0F 29 95", "1.26.20"}
};

// 3. reloadMaterial candidate
constexpr PatternCandidate reloadMaterialCandidates[] = {
    // 1.26.20 / 1.26.51 (Confirmed unique on 1.26.51.01 at RVA 0x5e21ba0)
    {"1.26.20/51", "C6 81 ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 75", "1.26.51.01"}
};

using ReadFileFn       = std::string* (*)(void*, std::string*, Core::PathView*);
using EndFrameFn       = void (*)(uintptr_t, uintptr_t);
using ReloadMaterialFn = void (*)(uintptr_t);

ReadFileFn       g_readFileOriginal = nullptr;
EndFrameFn       g_endFrameOriginal = nullptr;
ReloadMaterialFn g_reloadMaterial   = nullptr;

void* g_readFileTarget = nullptr;
void* g_endFrameTarget = nullptr;

std::string* readFileDetour(void* self, std::string* result, Core::PathView* path) {
    if (g_readFileOriginal == nullptr) {
        return result;
    }

    std::string* ret = g_readFileOriginal(self, result, path);
    if (ret == nullptr || path == nullptr) {
        return ret;
    }

    // Bypass inmediato si no hay shaderpack activo o si el cargador está en modo vainilla
    if (!MaterialLoader::getInstance().isOverrideActive()) {
        return ret;
    }

    try {
        auto overrideBytes = MaterialLoader::getInstance().resolveMaterial(path->getUtf8StringView());
        if (overrideBytes.has_value() && !overrideBytes->empty()) {
            ret->assign(reinterpret_cast<char const*>(overrideBytes->data()), overrideBytes->size());

            static std::atomic<int> logged{0};
            if (logged.fetch_add(1) < 5) {
                if (auto mod = ll::mod::NativeMod::current()) {
                    mod->getLogger().info(
                        "[MaterialHooks] Overrode '{}' ({} bytes)",
                        path->getUtf8StringView(),
                        overrideBytes->size()
                    );
                }
            }
        }
    } catch (...) {
        // Fail open: jamás lanzar excepciones a través de los límites del hook del juego
    }

    return ret;
}

void endFrameDetour(uintptr_t self, uintptr_t context) {
    if (g_reloadMaterial != nullptr && MaterialHooks::getInstance().consumeReload()) {
        try {
            g_reloadMaterial(self);
            if (auto mod = ll::mod::NativeMod::current()) {
                mod->getLogger().info("[MaterialHooks] RenderDragon materials reloaded");
            }
        } catch (...) {
            // Protección del hilo de renderizado
        }
    }

    if (g_endFrameOriginal != nullptr) {
        g_endFrameOriginal(self, context);
    }
}

// Bounded section locator for the target module
bool getModuleTextSection(HMODULE hModule, uintptr_t& outBase, size_t& outSize) {
    if (!hModule) return false;
    outBase = reinterpret_cast<uintptr_t>(hModule);

    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(outBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
        char secName[9] = {0};
        std::memcpy(secName, section->Name, 8);
        if (std::string_view(secName).starts_with(".text")) {
            outBase = reinterpret_cast<uintptr_t>(hModule) + section->VirtualAddress;
            outSize = section->Misc.VirtualSize;
            return true;
        }
    }

    // Fallback to full module image
    outBase = reinterpret_cast<uintptr_t>(hModule);
    outSize = ntHeaders->OptionalHeader.SizeOfImage;
    return true;
}

#endif // __has_include("ll/api/memory/Hook.h")

} // namespace

MaterialHooks& MaterialHooks::getInstance() {
    static MaterialHooks instance;
    return instance;
}

MaterialHooks::~MaterialHooks() {
    uninstall();
}

bool MaterialHooks::isInstalled() const {
    std::lock_guard lock(mMutex);
    return mInstalled;
}

bool MaterialHooks::areSignaturesVerified() const {
    std::lock_guard lock(mMutex);
    return mSignaturesVerified;
}

bool MaterialHooks::isReloadAvailable() const {
    std::lock_guard lock(mMutex);
    return mReloadAvailable;
}

std::string MaterialHooks::lastError() const {
    std::lock_guard lock(mMutex);
    return mLastError;
}

void MaterialHooks::queueReload() {
    std::lock_guard lock(mMutex);
    mReloadPending = true;
}

bool MaterialHooks::consumeReload() {
    std::lock_guard lock(mMutex);
    if (!mReloadPending) {
        return false;
    }
    mReloadPending = false;
    return true;
}

void MaterialHooks::restoreVanilla() {
    std::lock_guard lock(mMutex);
    mReloadPending = false;
    MaterialLoader::getInstance().restoreVanilla();
}

void MaterialHooks::verifySignatures() {
    std::lock_guard lock(mMutex);
    verifySignaturesLocked();
}

void MaterialHooks::verifySignaturesLocked() {
    mSignaturesVerified = false;

#if __has_include("ll/api/memory/Hook.h")
    auto mod = ll::mod::NativeMod::current();
    auto& compat = CompatibilityManager::getInstance();

    if (!compat.isVersionCompatible()) {
        mLastError = "Incompatible game version for RenderDragon hooks";
        SignatureDiagnostic diag;
        diag.status  = SignatureStatus::IncompatibleVersion;
        diag.details = mLastError;
        compat.setSignatureDiagnostic("readFile", diag);
        compat.setSignatureDiagnostic("endFrame", diag);
        compat.setSignatureDiagnostic("reloadMaterial", diag);
        return;
    }

    HMODULE hModule = GetModuleHandleW(nullptr);
    if (!hModule) {
        mLastError = "Failed to obtain main Minecraft.Windows.exe module handle";
        return;
    }

    uintptr_t modImageBase = reinterpret_cast<uintptr_t>(hModule);
    uintptr_t textBase = 0;
    size_t    textSize = 0;
    if (!getModuleTextSection(hModule, textBase, textSize)) {
        mLastError = "Failed to locate .text section in Minecraft.Windows.exe";
        return;
    }

    MemoryRegionInfo textRegion{textBase, textSize, true, true};
    std::span<uint8_t const> textSpan(reinterpret_cast<uint8_t const*>(textBase), textSize);

    // Helper para verificar una lista de patrones candidatos
    auto verifyCandidates = [&](std::string_view funcName, PatternCandidate const* candidates, size_t count) -> SignatureDiagnostic {
        SignatureDiagnostic diag;
        diag.name = std::string(funcName);

        for (size_t i = 0; i < count; ++i) {
            auto const& cand = candidates[i];
            auto parsed = SignatureVerifier::parsePattern(cand.pattern);
            auto matches = SignatureVerifier::scanSpan(textSpan, parsed, textBase, 16);

            diag.matchCount = matches.size();
            diag.matchedPattern = std::string(cand.pattern);

            if (matches.empty()) {
                continue; // Probar siguiente candidato
            }

            if (matches.size() > 1) {
                diag.status  = SignatureStatus::Ambiguous;
                diag.details = "Ambiguous signature: " + std::to_string(matches.size()) + " matches detected";
                return diag; // Rechazo inmediato por ambigüedad
            }

            // Exactamente 1 coincidencia
            uintptr_t targetAddr = matches[0];
            diag.address = targetAddr;
            diag.rva     = targetAddr - modImageBase;

            // Verificar límites de sección .text y permisos
            if (SignatureVerifier::verifyMemoryRegion(targetAddr, textRegion) != SignatureStatus::Verified) {
                diag.status  = SignatureStatus::InvalidMemoryRegion;
                diag.details = "Match address is outside .text section boundaries";
                return diag;
            }

            // Verificar permisos reales de memoria con VirtualQuery
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<void const*>(targetAddr), &mbi, sizeof(mbi)) != 0) {
                bool isCommitted = (mbi.State == MEM_COMMIT);
                DWORD prot = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE);
                bool isExec = (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
                               prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);

                if (!isCommitted || !isExec) {
                    diag.status  = SignatureStatus::InvalidMemoryRegion;
                    diag.details = "Match address is non-executable or uncommitted memory";
                    return diag;
                }
            }

            diag.status  = SignatureStatus::Verified;
            diag.details = "Candidate [" + std::string(cand.name) + "] uniquely verified at RVA +0x" +
                           [&] {
                               char buf[32];
                               snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(diag.rva));
                               return std::string(buf);
                           }();
            return diag;
        }

        diag.status  = SignatureStatus::NotFound;
        diag.details = "No candidate signatures matched in module .text section";
        return diag;
    };

    auto readDiag   = verifyCandidates("readFile", readFileCandidates, std::size(readFileCandidates));
    auto endDiag    = verifyCandidates("endFrame", endFrameCandidates, std::size(endFrameCandidates));
    auto reloadDiag = verifyCandidates("reloadMaterial", reloadMaterialCandidates, std::size(reloadMaterialCandidates));

    compat.setSignatureDiagnostic("readFile", readDiag);
    compat.setSignatureDiagnostic("endFrame", endDiag);
    compat.setSignatureDiagnostic("reloadMaterial", reloadDiag);

    if (mod) {
        auto& logger = mod->getLogger();
        logger.info("[SignatureValidation] readFile: {} (Matches: {}, RVA: +{:#x})",
            signatureStatusToString(readDiag.status), readDiag.matchCount, readDiag.rva);
        logger.info("[SignatureValidation] endFrame: {} (Matches: {}, RVA: +{:#x})",
            signatureStatusToString(endDiag.status), endDiag.matchCount, endDiag.rva);
        logger.info("[SignatureValidation] reloadMaterial: {} (Matches: {}, RVA: +{:#x})",
            signatureStatusToString(reloadDiag.status), reloadDiag.matchCount, reloadDiag.rva);
    }

    if (readDiag.status == SignatureStatus::Verified) {
        g_readFileTarget = reinterpret_cast<void*>(readDiag.address);
    }
    if (endDiag.status == SignatureStatus::Verified) {
        g_endFrameTarget = reinterpret_cast<void*>(endDiag.address);
    }
    if (reloadDiag.status == SignatureStatus::Verified) {
        g_reloadMaterial = reinterpret_cast<ReloadMaterialFn>(reloadDiag.address);
    }

    mSignaturesVerified = (readDiag.status == SignatureStatus::Verified);

    // Mapeo a estados de capacidad visibles
    auto const& safety = ConfigManager::getInstance().config().safety;
    if (safety.safeMode || !safety.enableExperimentalHooks) {
        // En modo seguro las firmas verificadas se reportan como tales pero con hooks desconectados
        compat.setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unknown,
            readDiag.status == SignatureStatus::Verified ? "cmd.info.reason.safemode_verified" : "cmd.info.reason.not_found"
        );
        compat.setCapabilityState(
            FeatureCapability::MaterialHotReload,
            CapabilityState::Unknown,
            (endDiag.status == SignatureStatus::Verified && reloadDiag.status == SignatureStatus::Verified)
                ? "cmd.info.reason.safemode_verified" : "cmd.info.reason.not_found"
        );
        compat.setCapabilityState(
            FeatureCapability::GraphicsModeSwitch,
            CapabilityState::Unknown,
            "cmd.info.reason.safemode"
        );
    } else {
        if (readDiag.status == SignatureStatus::Verified) {
            compat.setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Supported, "Signature verified");
        } else if (readDiag.status == SignatureStatus::Ambiguous) {
            compat.setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Unsupported, "cmd.info.reason.ambiguous");
        } else if (readDiag.status == SignatureStatus::InvalidMemoryRegion) {
            compat.setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Unsupported, "cmd.info.reason.invalid_memory");
        } else {
            compat.setCapabilityState(FeatureCapability::MaterialRedirection, CapabilityState::Unsupported, "cmd.info.reason.not_found");
        }

        if (endDiag.status == SignatureStatus::Verified && reloadDiag.status == SignatureStatus::Verified) {
            compat.setCapabilityState(FeatureCapability::MaterialHotReload, CapabilityState::Supported, "Signatures verified");
        } else {
            compat.setCapabilityState(FeatureCapability::MaterialHotReload, CapabilityState::Unsupported, "cmd.info.reason.not_found");
        }
    }
#endif
}

bool MaterialHooks::install() {
    std::lock_guard lock(mMutex);
    if (mInstalled) {
        return true;
    }

#if __has_include("ll/api/memory/Hook.h")
    auto mod = ll::mod::NativeMod::current();
    if (!mod) {
        mLastError = "NativeMod context unavailable";
        return false;
    }
    auto& logger = mod->getLogger();

    // 1. Ejecutar análisis y verificación rigurosa de firmas
    verifySignaturesLocked();

    // 2. MODO SEGURO: Ganchos experimentales desactivados por defecto
    auto const& safety = ConfigManager::getInstance().config().safety;
    if (safety.safeMode || !safety.enableExperimentalHooks) {
        mLastError = "RenderDragon native hooks disabled (safe mode active)";
        logger.info("MaterialHooks: {}. Signatures verified diagnostically; memory remains untouched.", mLastError);
        return false;
    }

    // 3. Verificar compatibilidad general de versión antes de tocar memoria
    if (!CompatibilityManager::getInstance().isVersionCompatible()) {
        mLastError = "Incompatible game version for RenderDragon hooks";
        logger.warn("MaterialHooks: {}; hooks safely aborted", mLastError);
        return false;
    }

    // 4. Regla estricta de Fase 3: no instalar si la firma no fue verificada inequívocamente
    auto readDiag = CompatibilityManager::getInstance().getSignatureDiagnostic("readFile");
    if (!readDiag.has_value() || readDiag->status != SignatureStatus::Verified || g_readFileTarget == nullptr) {
        mLastError = "readFile signature not verified unequivocally for Minecraft 1.26.51.01";
        logger.warn("MaterialHooks: {}. Detour blocked to protect client stability.", mLastError);
        return false;
    }

    // Instalar hook de readFile
    ll::memory::hook(
        g_readFileTarget,
        ll::memory::toFuncPtr(&readFileDetour),
        reinterpret_cast<ll::memory::FuncPtr*>(&g_readFileOriginal),
        ll::memory::HookPriority::Normal
    );

    if (g_readFileOriginal == nullptr) {
        mLastError = "Hook installation on readFile target failed";
        logger.error("MaterialHooks: {}", mLastError);
        CompatibilityManager::getInstance().setCapabilityState(
            FeatureCapability::MaterialRedirection,
            CapabilityState::Unsupported,
            mLastError
        );
        return false;
    }

    logger.info("MaterialHooks: readFile detour hooked successfully at {:#x}", reinterpret_cast<uintptr_t>(g_readFileTarget));
    CompatibilityManager::getInstance().setCapabilityState(
        FeatureCapability::MaterialRedirection,
        CapabilityState::Supported,
        "readFile detour installed"
    );

    // 5. endFrame hook
    auto endDiag = CompatibilityManager::getInstance().getSignatureDiagnostic("endFrame");
    if (endDiag.has_value() && endDiag->status == SignatureStatus::Verified && g_endFrameTarget != nullptr) {
        ll::memory::hook(
            g_endFrameTarget,
            ll::memory::toFuncPtr(&endFrameDetour),
            reinterpret_cast<ll::memory::FuncPtr*>(&g_endFrameOriginal),
            ll::memory::HookPriority::Normal
        );

        if (g_endFrameOriginal != nullptr) {
            logger.info("MaterialHooks: endFrame detour hooked successfully");
        } else {
            logger.warn("MaterialHooks: endFrame hook installation failed");
        }
    }

    mInstalled       = true;
    mReloadAvailable = (g_reloadMaterial != nullptr && g_endFrameOriginal != nullptr);
    return true;

#else
    mLastError = "LeviLamina Hook API headers unavailable";
    return false;
#endif
}

void MaterialHooks::uninstall() {
    std::lock_guard lock(mMutex);
    if (!mInstalled) {
        return;
    }

#if __has_include("ll/api/memory/Hook.h")
    if (g_readFileOriginal != nullptr && g_readFileTarget != nullptr) {
        ll::memory::unhook(g_readFileTarget, ll::memory::toFuncPtr(&readFileDetour));
        g_readFileOriginal = nullptr;
        g_readFileTarget   = nullptr;
    }

    if (g_endFrameOriginal != nullptr && g_endFrameTarget != nullptr) {
        ll::memory::unhook(g_endFrameTarget, ll::memory::toFuncPtr(&endFrameDetour));
        g_endFrameOriginal = nullptr;
        g_endFrameTarget   = nullptr;
    }

    g_reloadMaterial = nullptr;
#endif

    mInstalled          = false;
    mSignaturesVerified = false;
    mReloadAvailable    = false;
    mReloadPending      = false;
}

} // namespace aeris
