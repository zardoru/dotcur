#pragma once

class Transformation
{
    Mat4   mMatrix;
    float  mWidth, mHeight;
    Vec2   mPosition;
    uint32_t mLayer;

    Vec2 mScale;
    float mRotation;
    bool   mDirtyMatrix;

	bool IsMatrixDirty();
    Transformation* Chain;
public:
    Transformation();

    // Scale
    void SetScale(Vec2 Scale);
    void SetScale(float Scale);
    void SetScaleX(float ScaleX);
    void SetScaleY(float ScaleY);
    float GetScaleX() const;
    float GetScaleY() const;
    Vec2 GetScale() const;

    // Position
    void SetPosition(Vec2 Pos);
    void SetPosition(float pX, float pY);
    void AddPosition(Vec2 pos);
    void AddPosition(float pX, float pY);
    void SetPositionX(float pX);
    void SetPositionY(float pY);
    void AddPositionX(float pX);
    void AddPositionY(float pY);
    Vec2 GetPosition() const;
    float GetPositionX() const;
    float GetPositionY() const;
    void SetZ(uint32_t Z);
    uint32_t GetZ() const;

    // Size
    void SetSize(Vec2 Size);
    void SetSize(float Size);
    void SetSize(float W, float H);
    void SetWidth(float W);
    void SetHeight(float H);
    float GetWidth() const;
    float GetHeight() const;
    Vec2 GetSize() const;

    // Rotation
    void SetRotation(float Rot);
    float GetRotation() const;
    void AddRotation(float Rot);

    void ChainTransformation(Transformation *Other);

    const glm::mat4 &GetMatrix();
    void UpdateMatrix();
};