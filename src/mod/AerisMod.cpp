#include "mod/AerisMod.h"

#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/command/ClientCommandRegisterEvent.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/server/ServerStartedEvent.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CommandFlag.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"

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

void AerisMod::executeAerisCommand(std::string_view args, CommandOrigin* origin, Player* player) {
    auto& logger = getSelf().getLogger();

    auto sendLine = [&](std::string const& line) {
        if (player != nullptr) {
            player->sendMessage(line);
        } else if (origin != nullptr) {
            if (auto* actor = origin->getEntity()) {
                if (auto* p = static_cast<Player*>(actor)) {
                    p->sendMessage(line);
                    return;
                }
            }
            origin->handleCommandOutputCallback(1, std::string(line));
        }
        logger.info("[Aeris Command] {}", line);
    };

    if (args.empty() || args == "help") {
        sendLine(trFormat("cmd.aeris.header", {"0.1.0-alpha"}));
        sendLine(trFormat("cmd.aeris.desc", {std::string(CompatibilityManager::getInstance().targetVersion())}));
        sendLine(tr("cmd.aeris.commands"));
        sendLine(tr("cmd.help.aeris"));
        sendLine(tr("cmd.help.scan"));
        sendLine(tr("cmd.help.list"));
        sendLine(tr("cmd.help.rollback"));
        sendLine(tr("cmd.help.info"));
        sendLine(tr("cmd.help.version"));
    } else if (args == "info") {
        auto const& compat = CompatibilityManager::getInstance();
        sendLine(tr("cmd.info.header"));

        std::string verDisplay = compat.detectedVersion();
        if (!compat.rawDetectedVersion().empty() && compat.rawDetectedVersion() != compat.detectedVersion()) {
            verDisplay += " (" + compat.rawDetectedVersion() + ")";
        }

        sendLine(trFormat("cmd.info.detected", {verDisplay}));
        sendLine(trFormat("cmd.info.loader", {compat.detectedLoaderVersion()}));
        sendLine(trFormat("cmd.info.target", {std::string(compat.targetVersion())}));
        sendLine(trFormat("cmd.info.compatible", {compat.isVersionCompatible() ? tr("cmd.info.yes") : tr("cmd.info.no")}));

        auto redirState  = compat.getCapabilityState(FeatureCapability::MaterialRedirection);
        auto redirReason = compat.getCapabilityReason(FeatureCapability::MaterialRedirection);
        sendLine(trFormat("cmd.info.materialRedirection", {formatCapabilityDisplay(redirState, redirReason)}));

        auto hotState  = compat.getCapabilityState(FeatureCapability::MaterialHotReload);
        auto hotReason = compat.getCapabilityReason(FeatureCapability::MaterialHotReload);
        sendLine(trFormat("cmd.info.hotReload", {formatCapabilityDisplay(hotState, hotReason)}));

        auto switchState  = compat.getCapabilityState(FeatureCapability::GraphicsModeSwitch);
        auto switchReason = compat.getCapabilityReason(FeatureCapability::GraphicsModeSwitch);
        sendLine(trFormat("cmd.info.graphicsSwitch", {formatCapabilityDisplay(switchState, switchReason)}));
    } else if (args == "scan") {
        auto pDir = getPacksDir();
        auto gDir = getGeneratedDir();
        ShaderPackManager::getInstance().scan(pDir, gDir);
        auto count = std::to_string(ShaderPackManager::getInstance().packs().size());
        sendLine(trFormat("cmd.scan.success", {count}));
    } else if (args == "list") {
        auto const& packs = ShaderPackManager::getInstance().packs();
        sendLine(trFormat("cmd.list.header", {std::to_string(packs.size())}));

        if (packs.empty()) {
            sendLine(tr("cmd.list.empty"));
        } else {
            auto const* active = ShaderPackManager::getInstance().activePack();
            for (auto const& p : packs) {
                std::string mode = renderModeToString(p.renderMode);
                std::string paramCount = std::to_string(p.parameters.size());
                if (active && active->name == p.name) {
                    sendLine(trFormat("cmd.list.item", {p.name, mode, paramCount, tr("Shaders: Enabled")}));
                } else {
                    sendLine(trFormat("cmd.list.item_inactive", {p.name, mode, paramCount}));
                }
            }
        }
    } else if (args == "rollback") {
        if (!MaterialLoader::getInstance().isOverrideActive() &&
            ShaderPackManager::getInstance().activePack() == nullptr) {
            sendLine(tr("cmd.rollback.already_vanilla"));
        } else {
            MaterialHooks::getInstance().restoreVanilla();
            ShaderPackManager::getInstance().deactivatePack();
            sendLine(tr("cmd.rollback.success"));
        }
    } else if (args == "version") {
        sendLine(trFormat("cmd.version.text", {"0.1.0-alpha"}));
    } else {
        sendLine("§c[Aeris] " + tr("cmd.help.aeris"));
    }
}

void AerisMod::registerCommands(bool isClient) {
    auto& logger = getSelf().getLogger();
    try {
        auto& registrar = ll::command::CommandRegistrar::getInstance(isClient);
        auto& cmd = registrar.getOrCreateCommand(
            "aeris",
            "Aeris Shaders & Rendering Manager",
            CommandPermissionLevel::Any,
            CommandFlagValue::NotCheat
        );

        cmd.overload().execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("help").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("help", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("scan").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("scan", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("list").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("list", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("rollback").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("rollback", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("info").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("info", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        cmd.overload().text("version").execute([this](CommandOrigin const& origin, CommandOutput&) {
            executeAerisCommand("version", const_cast<CommandOrigin*>(&origin), nullptr);
        });

        logger.info("Aeris commands registered on {} command registry", isClient ? "client" : "server");
    } catch (std::exception const& e) {
        logger.warn("Failed to register Aeris commands on {} registry: {}", isClient ? "client" : "server", e.what());
    } catch (...) {
        logger.warn("Unknown error registering Aeris commands on {} registry", isClient ? "client" : "server");
    }
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

    // 5. Suscribir listeners tempranamente en load()
    auto& bus = ll::event::EventBus::getInstance();

    // Interceptor principal de comandos de chat (cancela 'Comando desconocido' y ejecuta /aeris de forma infalible)
    mExecutingCommandListener = bus.emplaceListener<ll::event::command::ExecutingCommandEvent>(
        [this](ll::event::command::ExecutingCommandEvent& ev) {
            std::string cmd = ev.commandContext().mCommand;
            size_t start = 0;
            while (start < cmd.size() && (cmd[start] == '/' || cmd[start] == ' ')) {
                start++;
            }
            std::string_view trimmed = std::string_view(cmd).substr(start);
            if (trimmed.starts_with("aeris")) {
                ev.cancel();
                std::string_view sub = trimmed.substr(5);
                while (!sub.empty() && sub.front() == ' ') {
                    sub.remove_prefix(1);
                }
                executeAerisCommand(sub, ev.commandContext().mOrigin.get(), nullptr);
            }
        }
    );

    // Interceptor de mensajes de chat estándar (soporta .aeris o si se envía como chat packet)
    mPlayerChatListener = bus.emplaceListener<ll::event::player::PlayerChatEvent>(
        [this](ll::event::player::PlayerChatEvent& ev) {
            std::string_view msg = ev.message();
            size_t start = 0;
            while (start < msg.size() && (msg[start] == '/' || msg[start] == '.' || msg[start] == '!' || msg[start] == ' ')) {
                start++;
            }
            std::string_view trimmed = msg.substr(start);
            if (trimmed.starts_with("aeris")) {
                ev.cancel();
                std::string_view sub = trimmed.substr(5);
                while (!sub.empty() && sub.front() == ' ') {
                    sub.remove_prefix(1);
                }
                executeAerisCommand(sub, nullptr, &ev.self());
            }
        }
    );

    mServerCommandListener = bus.emplaceListener<ll::event::command::ServerCommandRegisterEvent>(
        [this](ll::event::command::ServerCommandRegisterEvent& ev) {
            getSelf().getLogger().info("ServerCommandRegisterEvent received (clientHosted: {})", ev.isClientHostedServer());
            registerCommands(false);
        }
    );

    mClientCommandListener = bus.emplaceListener<ll::event::command::ClientCommandRegisterEvent>(
        [this](ll::event::command::ClientCommandRegisterEvent&) {
            getSelf().getLogger().info("ClientCommandRegisterEvent received");
            registerCommands(true);
        }
    );

    mServerStartedListener = bus.emplaceListener<ll::event::server::ServerStartedEvent>(
        [this](ll::event::server::ServerStartedEvent&) {
            getSelf().getLogger().info("ServerStartedEvent received, registering commands on server");
            registerCommands(false);
        }
    );

    mClientJoinLevelListener = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
        [this](ll::event::client::ClientJoinLevelEvent&) {
            getSelf().getLogger().info("ClientJoinLevelEvent received, registering commands");
            registerCommands(false);
            registerCommands(true);
        }
    );

    logger.info("Aeris loaded successfully [Language: {}, Detected: {}, Compatible: {}]",
        ConfigManager::getInstance().config().language,
        CompatibilityManager::getInstance().rawDetectedVersion().empty() ?
            CompatibilityManager::getInstance().detectedVersion() :
            CompatibilityManager::getInstance().rawDetectedVersion(),
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

    // 2. Comprobar instalación de hooks nativos (bloqueados por defecto en modo seguro)
    MaterialHooks::getInstance().install();

    // 3. Registrar en el cliente durante enable() si el registro de cliente está activo
    registerCommands(true);

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
    auto& bus = ll::event::EventBus::getInstance();
    if (mExecutingCommandListener) { bus.removeListener(mExecutingCommandListener); mExecutingCommandListener.reset(); }
    if (mPlayerChatListener)       { bus.removeListener(mPlayerChatListener);       mPlayerChatListener.reset(); }
    if (mServerCommandListener)    { bus.removeListener(mServerCommandListener);    mServerCommandListener.reset(); }
    if (mClientCommandListener)    { bus.removeListener(mClientCommandListener);    mClientCommandListener.reset(); }
    if (mServerStartedListener)    { bus.removeListener(mServerStartedListener);    mServerStartedListener.reset(); }
    if (mClientJoinLevelListener)  { bus.removeListener(mClientJoinLevelListener);  mClientJoinLevelListener.reset(); }

    return true;
}

} // namespace aeris

LL_REGISTER_MOD(aeris::AerisMod, aeris::AerisMod::getInstance());
