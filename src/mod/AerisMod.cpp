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
#include "localization/LocalizationManager.h"
#include "optimization/PerformanceManager.h"

namespace aeris {

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

    // 1. Cargar configuración
    auto configPath = getSelf().getModDir() / "config" / "config.json";
    if (!ConfigManager::getInstance().load(configPath)) {
        logger.warn("Failed to load config from {}; default settings will be used", configPath.string());
    }

    // 2. Inicializar localización
    auto langDir = getSelf().getModDir() / "lang";
    LocalizationManager::getInstance().load(langDir, ConfigManager::getInstance().config().language);

    // 3. Inicializar capa de compatibilidad
    CompatibilityManager::getInstance().initialize();

    // 4. Inicializar subsistema de rendimiento
    PerformanceManager::getInstance().initialize();

    logger.info("Aeris loaded successfully [Language: {}, Target: {}]",
        ConfigManager::getInstance().config().language,
        CompatibilityManager::getInstance().targetVersion()
    );
    return true;
}

bool AerisMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("Aeris enabling...");

    // Asegurar existencia de directorios de datos
    static_cast<void>(getPacksDir());
    static_cast<void>(getGeneratedDir());

    // Registrar comandos de cliente
    ll::event::EventBus::getInstance().emplaceListener<ll::event::command::ClientCommandRegisterEvent>(
        [](ll::event::command::ClientCommandRegisterEvent&) {
            auto& cmd = ll::command::CommandRegistrar::getInstance(true).getOrCreateCommand(
                "aeris",
                "Aeris Shaders & Rendering Manager",
                CommandPermissionLevel::Any
            );

            // /aeris (ayuda / información)
            cmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
                output.success("§6[Aeris]§r v{} by ByCesarDev", "0.1.0-alpha");
                output.success("Target: Minecraft 1.26.51.01 | Shaders & Rendering");
                output.success("Commands: /aeris help, /aeris info, /aeris version");
            });

            cmd.overload().text("help").execute([](CommandOrigin const&, CommandOutput& output) {
                output.success("§6=== Aeris Help ===§r");
                output.success("/aeris - Show mod overview");
                output.success("/aeris info - Show system and compatibility info");
                output.success("/aeris version - Show current version");
            });

            cmd.overload().text("info").execute([](CommandOrigin const&, CommandOutput& output) {
                auto const& compat = CompatibilityManager::getInstance();
                output.success("§6=== Aeris System Info ===§r");
                output.success("Detected: {}", compat.detectedVersion());
                output.success("Target: {}", compat.targetVersion());
                output.success("Compatible: {}", compat.isVersionCompatible() ? "§aYes§r" : "§cNo§r");
                output.success("Graphics Switch: {}", compat.isGraphicsSwitchSupported() ? "§aReady§r" : "§cUnavailable§r");
            });

            cmd.overload().text("version").execute([](CommandOrigin const&, CommandOutput& output) {
                output.success("Aeris v{} (Client Mod)", "0.1.0-alpha");
            });
        }
    );

    logger.info("Aeris enabled successfully");
    return true;
}

bool AerisMod::disable() {
    auto& logger = getSelf().getLogger();
    logger.info("Aeris disabling...");
    return true;
}

} // namespace aeris

LL_REGISTER_MOD(aeris::AerisMod, aeris::AerisMod::getInstance());
