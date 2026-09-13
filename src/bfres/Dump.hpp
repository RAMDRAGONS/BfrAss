#pragma once

#include "bfres/ResFile.hpp"

#include <string>
#include <vector>

namespace bfrass::bfres {

// Decoded shader parameter values in file order (bools/ints as integers).
std::vector<double> shaderParamValues(const ShaderParam& param, Endian endian);

// "name: v1, v2 | v3, v4" style text matching the original importer's listener output.
std::string formatShaderParam(const ShaderParam& param, Endian endian);

std::string formatRenderInfo(const RenderInfo& info);
std::string formatUserData(const UserData& data);

// Prints the "Texture properties for <material>" block for the material.
void printMaterialInfo(const Material& material, Endian endian, const Log& log);

// One line per model with shape/material/bone counts, used by `bfrass info`.
std::vector<std::string> describeResFile(const ResFile& file);

} // namespace bfrass::bfres
