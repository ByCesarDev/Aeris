#include "mod/AerisMod.h"

#include <filesystem>
#include <system_error>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ClientCommandRegisterEvent.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include "core/CompatibilityManager.h"
#include "core/ConfigManager.h"
#include "hooks/MaterialHooks.h"
#include "localization/LocalizationManager.h"
#include "optimization/PerformanceManager.h"
#include "shaders/MaterialLoader.h"
#include "shaders/ShaderPackManager.h"

namespace aeris {

namespace {

std::string formatCapabilityDisplay(CapabilityState state, std::string const& reason) {
    switch (state) {
    case CapabilityState::Supported:
        return tr("cmd.info.state.supported");
    case CapabilityState::Unsupported:
        return trFormat("cmd.info.state.unsupported", {reason});
    case CapabilityState::Unknown:
    default:
        return trFormat("cmd.info.state.unknown", {reason});
    }
}

} // namespace

AerisMod& AerisMod::getInstance() {
    static AerisMod instance;
    return instance;
}

std::filesystem::path AerisMod::getPacksDir() const {
    auto dir = mSelf.getModDir() / ConfigManager::getInstance().config().dataDir / ConfigManager::getInstance().config().shaderPacksDir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path AerisMod::getGeneratedDir() const {
    auto dir = mSelf.getModDir() / ConfigManager::getInstance().config().dataDir / ConfigManager::getInstance().config().generatedDir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool AerisMod::load() {
    auto& logger = getSelf().getLogger();
    logger.info("Aeris v{} loading...", "0.1.0-alpha");

    // 1. Cargar configuración con validación transaccional
    auto configPath = getSelf().getModDir() / "config" / "config.json";
    if (!ConfigManager::getInstance().load(configPath)) {
        logger.warn("Failed to load config from {}: {}; default settings will be used",
            configPath.string(), ConfigManager::getInstance().lastError());
    }

    // 2. Inicializar localización bilingüe
    auto langDir = getSelf().getModDir() / "lang";
    LocalizationManager::getInstance().load(langDir, ConfigManager::getInstance().config().language);

    // 3. Inicializar capa de compatibilidad con detección real de versión
    CompatibilityManager::getInstance().initialize();

    // 4. Inicializar subsistema de rendimiento (sin métricas simuladas)
    PerformanceManager::getInstance().initialize();

    logger.info("Aeris loaded successfully [Language: {}, Detected: {}, Compatible: {}]",
        ConfigManager::getInstance().config().language,
        CompatibilityManager::getInstance().detectedVersion(),
        CompatibilityManager::getInstance().isVersionCompatible() ? "yes" : "no"
    );
    return true;
}

bool AerisMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("Aeris enabling...");

    // 1. Asegurar existencia de directorios de datos y escanear paquetes
    auto packsDir = getPacksDir();
    auto genDir   = getGeneratedDir();
    ShaderPackManager::getInstance().scan(packsDir, genDir);

    // 2. Intentar inicializar ganchos nativos con verificación estricta de seguridad
    // NOTA: No activa shaders por defecto. El juego inicia en 100% vainilla.
    if (!MaterialHooks::getInstance().install()) {
        logger.info("MaterialHooks: Hooks are currently inactive or safely bypassed [{}]",
            MaterialHooks::getInstance().lastError().empty() ? "Unverified environment" : MaterialHooks::getInstance().lastError());
    }

    // 3. Registrar comandos de cliente internacionalizados
    mCommandListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::command::ClientCommandRegisterEvent>(
        [](ll::event::command::ClientCommandRegisterEvent&) {
            auto& cmd = ll::command::CommandRegistrar::getInstance(true).getOrCreateCommand(
                "aeris",
                "Aeris Shaders & Rendering Manager",
                CommandPermissionLevel::Any
            );

            // /aeris (ayuda / información general)
            cmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
                output.success(trFormat("cmd.aeris.header", {"0.1.0-alpha"}));
                output.success(trFormat("cmd.aeris.desc", {std::string(CompatibilityManager::getInstance().targetVersion())}));
                output.success(tr("cmd.aeris.commands"));
            });

            cmd.overload().text("help").execute([](CommandOrigin const&, CommandOutput& output) {
                output.success(tr("cmd.help.header"));
                output.success(tr("cmd.help.aeris"));
                output.success(tr("cmd.help.scan"));
                output.success(tr("cmd.help.list"));
                output.success(tr("cmd.help.rollback"));
                output.success(tr("cmd.help.info"));
                output.success(tr("cmd.help.version"));
            });

            cmd.overload().text("scan").execute([](CommandOrigin const&, CommandOutput& output) {
                auto pDir = AerisMod::getInstance().getPacksDir();
                auto gDir = AerisMod::getInstance().getGeneratedDir();
                ShaderPackManager::getInstance().scan(pDir, gDir);
                auto count = std::to_string(ShaderPackManager::getInstance().packs().size());
                output.success(trFormat("cmd.scan.success", {count}));
            });

            cmd.overload().text("list").execute([](CommandOrigin const&, CommandOutput& output) {
                auto const& packs = ShaderPackManager::getInstance().packs();
                output.success(trFormat("cmd.list.header", {std::to_string(packs.size())}));

                if (packs.empty()) {
                    output.success(tr("cmd.list.empty"));
                    return;
                }

                auto const* active = ShaderPackManager::getInstance().activePack();
                for (auto const& p : packs) {
                    std::string mode = renderModeToString(p.renderMode);
                    std::string paramCount = std::to_string(p.parameters.size());
                    if (active && active->name == p.name) {
                        output.success(trFormat("cmd.list.item", {p.name, mode, paramCount, tr("Shaders: Enabled")}));
                    } else {
                        output.success(trFormat("cmd.list.item_inactive", {p.name, mode, paramCount}));
                    }
                }
            });

            cmd.overload().text("rollback").execute([](CommandOrigin const&, CommandOutput& output) {
                if (!MaterialLoader::getInstance().isOverrideActive() &&
                    ShaderPackManager::getInstance().activePack() == nullptr) {
                    output.success(tr("cmd.rollback.already_vanilla"));
                    return;
                }

                MaterialHooks::getInstance().restoreVanilla();
                ShaderPackManager::getInstance().deactivatePack();
                output.success(tr("cmd.rollback.success"));
            });

            cmd.overload().text("info").execute([](CommandOrigin const&, CommandOutput& output) {
                auto const& compat = CompatibilityManager::getInstance();
                output.success(tr("cmd.info.header"));
                output.success(trFormat("cmd.info.detected", {compat.detectedVersion()}));
                output.success(trFormat("cmd.info.loader", {compat.detectedLoaderVersion()}));
                output.success(trFormat("cmd.info.target", {std::string(compat.targetVersion())}));
                output.success(trFormat("cmd.info.compatible", {compat.isVersionCompatible() ? tr("cmd.info.yes") : tr("cmd.info.no")}));

                auto redirState  = compat.getCapabilityState(FeatureCapability::MaterialRedirection);
                auto redirReason = compat.getCapabilityReason(FeatureCapability::MaterialRedirection);
                output.success(trFormat("cmd.info.materialRedirection", {formatCapabilityDisplay(redirState, redirReason)}));

                auto hotState  = compat.getCapabilityState(FeatureCapability::MaterialHotReload);
                auto hotReason = compat.getCapabilityReason(FeatureCapability::MaterialHotReload);
                output.success(trFormat("cmd.info.hotReload", {formatCapabilityDisplay(hotState, hotReason)}));

                auto switchState  = compat.getCapabilityState(FeatureCapability::GraphicsModeSwitch);
                auto switchReason = compat.getCapabilityReason(FeatureCapability::GraphicsModeSwitch);
                output.success(trFormat("cmd.info.graphicsSwitch", {formatCapabilityDisplay(switchState, switchReason)}));
            });

            cmd.overload().text("version").execute([](CommandOrigin const&, CommandOutput& output) {
                output.success(trFormat("cmd.version.text", {"0.1.0-alpha"}));
            });
        }
    );

    logger.info("Aeris enabled successfully");
    return true;
}

bool AerisMod::disable() {
    auto& logger = getSelf().getLogger();
    logger.info("Aeris disabling...");

    // 1. Restaurar materiales vainilla y desenganchar detours de memoria
    MaterialHooks::getInstance().restoreVanilla();
    MaterialHooks::getInstance().uninstall();

    // 2. Liberar suscripción a comandos de forma limpia
    if (mCommandListener) {
        ll::event::EventBus::getInstance().removeListener(mCommandListener);
        mCommandListener.reset();
    }

    return true;
}

} // namespace aeris

LL_REGISTER_MOD(aeris::AerisMod, aeris::AerisMod::getInstance());
