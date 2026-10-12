-- Aeris — Shaders, Rendering & Performance for Minecraft Bedrock (LeviLamina Client)
-- Target: Minecraft Bedrock 1.26.51.01 (GDK Windows x64) + LeviLamina Client SDK 26.51.6

add_rules("mode.debug", "mode.release")
set_license("LGPL-3.0")

local aerisVersion = "0.1.0-alpha"

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

includes("packages/levilamina-client-sdk.lua")
add_requires("levilamina-client-sdk 26.51.6", {configs = {shared = true}})
add_requires("levibuildscript")
add_requires("nlohmann_json v3.12.0")
add_requires("miniz 3.1.1")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("Aeris")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modVersion = aerisVersion})
    add_defines('AERIS_VERSION="' .. aerisVersion .. '"')

    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        add_syslinks("user32", "shell32", "comdlg32")
        set_exceptions("none") -- Avoid conflict with /EHa
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end

    add_packages("levilamina-client-sdk", "nlohmann_json", "miniz")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")

    after_build(function(target)
        local modName = target:name()
        local binDir  = path.join(os.projectdir(), "bin", modName)

        -- Copiar traducciones (lang/*.json)
        local langSrc = path.join(os.projectdir(), "lang")
        if os.isdir(langSrc) then
            local langDst = path.join(binDir, "lang")
            os.mkdir(langDst)
            for _, f in ipairs(os.files(path.join(langSrc, "*.json"))) do
                os.cp(f, langDst)
            end
        end

        -- Copiar configuración por defecto (config/config.json)
        local configSrc = path.join(os.projectdir(), "config", "config.json")
        if os.isfile(configSrc) then
            local configDst = path.join(binDir, "config")
            os.mkdir(configDst)
            os.cp(configSrc, path.join(configDst, "config.json"))
        end

        -- Copiar paquete de recursos para botón en Menú de Pausa (resources/ -> resource_packs/Aeris-ui/)
        local resSrc = path.join(os.projectdir(), "resources")
        if os.isdir(resSrc) then
            local resDst = path.join(binDir, "resource_packs", "Aeris-ui")
            os.tryrm(resDst)
            os.cp(resSrc, resDst)
        end
    end)

target("AerisTests")
    set_kind("binary")
    set_languages("c++20")
    set_symbols("debug")

    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        add_cxflags("/utf-8", "/W4")
    end

    add_packages("nlohmann_json", "miniz")
    add_includedirs("src", "tests")
    add_headerfiles("tests/**.h", "src/**.h")
    add_files("tests/**.cpp")
    add_files(
        "src/core/ConfigManager.cpp",
        "src/core/CompatibilityManager.cpp",
        "src/localization/LocalizationManager.cpp",
        "src/optimization/PerformanceManager.cpp",
        "src/shaders/ShaderPackParser.cpp",
        "src/shaders/ShaderPackManager.cpp",
        "src/shaders/MaterialLoader.cpp",
        "src/ui/UiLayout.cpp",
        "src/ui/UiScreen.cpp"
    )
