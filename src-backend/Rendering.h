#pragma once

#include "Transformation.h"

enum EBlendMode
{
    BLEND_ADD,
    BLEND_ALPHA,
    BLEND_MULTIPLY
};

class Texture;
class VBO;

namespace Renderer {
	void InitializeRender();
	void SetShaderParameters(bool InvertColor,
		bool UseGlobalLight, bool Centered, bool UseSecondTransformationMatrix = false,
		bool BlackToTransparent = false, bool ReplaceColor = false,
		int8_t HiddenMode = -1);

	void SetTextureParameters(std::string param_src);

	void SetPrimitiveQuadVBO();
	void FinalizeDraw();
	void DoQuadDraw();
	void SetBlendingMode(EBlendMode Mode);
	void SetTexturedQuadVBO(VBO *TexQuad);
	void DrawTexturedQuad(Texture* ToDraw, const AABB& TextureCrop, const Transformation& QuadTransformation, const EBlendMode &Mode = BLEND_ALPHA, const ColorRGB &InColor = Color::White);
	void DrawPrimitiveQuad(Transformation &QuadTransformation, const EBlendMode &Mode = BLEND_ALPHA, const ColorRGB &InColor = Color::White);

	void SetScissor(bool enable);
	void SetScissorRegion(int x, int y, int w, int h);
	void SetScissorRegionWnd(int x, int y, int w, int h);

	VBO* GetDefaultGeometryBuffer();
	VBO* GetDefaultTextureBuffer();
	VBO* GetDefaultColorBuffer();

	Texture* GetXorTexture();
}

inline float l2gamma (float c) {
	/*if (c > 1.) return 1.;
	else if (c < 0.) return 0.;
	else */
	if (c <= 0.04045) return c / 12.92;
	else return pow((c + 0.055) / 1.055, 2.4);
};