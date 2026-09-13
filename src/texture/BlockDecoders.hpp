#pragma once

#include <cstdint>

// Per-block decoders for the compressed formats found in BNTX/FTEX textures.
// Colour outputs are 16 RGBA8 texels (row major) unless noted otherwise.

namespace bfrass::tex::block {

void decodeBc1(const uint8_t* block, uint8_t* rgba);
void decodeBc2(const uint8_t* block, uint8_t* rgba);
void decodeBc3(const uint8_t* block, uint8_t* rgba);
// Writes 16 values in [-1, 1] for signed data or [0, 1] otherwise.
void decodeBc4(const uint8_t* block, float* values, bool isSigned);
void decodeBc7(const uint8_t* block, uint8_t* rgba);
// Writes 16 RGB triplets of linear floating point colour.
void decodeBc6h(const uint8_t* block, float* rgb, bool isSigned);

// ETC1/ETC2 RGB (with optional punch-through alpha), ETC2 EAC alpha and EAC R11.
void decodeEtc2Rgb(const uint8_t* block, uint8_t* rgba, bool punchThrough);
void decodeEacAlpha(const uint8_t* block, uint8_t* rgba);
void decodeEacR11(const uint8_t* block, float* values, bool isSigned);

// Decodes an ASTC LDR block into blockWidth * blockHeight RGBA8 texels.
// Returns false for void-extent/illegal encodings, which are filled with magenta.
bool decodeAstc(const uint8_t* block, uint32_t blockWidth, uint32_t blockHeight, uint8_t* rgba);

} // namespace bfrass::tex::block
