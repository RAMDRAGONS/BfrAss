#pragma once

#include "importer/ImportConfig.hpp"

#include <assimp/BaseImporter.h>

namespace Assimp {
class Importer;
}

namespace bfrass::importer {

// Assimp loader for NintendoWare BFRES model archives (Wii U FRES 3.x/4.x and
// Switch FRES 0.0 to 10.x), including their embedded BNTX/FTEX textures.
class BfresImporter : public Assimp::BaseImporter {
public:
    bool CanRead(const std::string& file, Assimp::IOSystem* io, bool checkSig) const override;
    const aiImporterDesc* GetInfo() const override;
    void SetupProperties(const Assimp::Importer* importer) override;

protected:
    void InternReadFile(const std::string& file, aiScene* scene, Assimp::IOSystem* io) override;

private:
    ImportConfig config_;
    void* logPointer_ = nullptr;
};

// Registers a new BfresImporter with the importer, which takes ownership of it.
void registerImporter(Assimp::Importer& importer);

} // namespace bfrass::importer
