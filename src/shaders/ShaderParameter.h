#pragma once

#include <string>
#include <vector>

namespace aeris {

enum class ParameterType {
    Bool,
    Slider,
    Enum,
    Color,
    Category
};

enum class ParameterSpan {
    Half,
    Full
};

struct EnumOption {
    std::string value; // Valor guardado / macro
    std::string label; // Etiqueta legible (o clave de traducción)
};

struct ShaderParameter {
    ParameterType type = ParameterType::Bool;
    ParameterSpan span = ParameterSpan::Half;

    std::string name;         // Nombre de la macro/clave (ej: VOLUMETRIC_CLOUDS)
    std::string label;        // Nombre visible en UI
    std::string description;  // Descripción o tooltip
    std::string current;      // Valor actual en formato cadena
    std::string defaultValue; // Valor por defecto

    // Slider
    double minVal    = 0.0;
    double maxVal    = 1.0;
    double stepVal   = 0.1;
    bool   isInteger = false;

    // Enum
    std::vector<EnumOption> options;

    // Color: lista de valores hex "#RRGGBB"
    std::vector<std::string> colorPalette;

    // Jerarquía de pantallas / categorías
    std::string              parentScreen;
    std::vector<std::string> childNames;
};

} // namespace aeris
