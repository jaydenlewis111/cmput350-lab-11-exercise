#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <vector>

#include "basic_transform.h"
#include "glad/glad.h"
#include "texture_atlas.h"
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#define GL_SILENCE_DEPRECATION  // silences macOS OpenGL deprecation warnings

#define glCheck(expr)                                                                              \
    expr;                                                                                          \
    if (int e = glGetError()) {                                                                    \
        std::cerr << "OpenGL error: " << e << " in file " __FILE__ << ", line number " << __LINE__ \
                  << '\n';                                                                         \
        abort();                                                                                   \
    }

using SpriteID = size_t;

class SpriteManager {
public:
    static constexpr size_t VERTICES_PER_SPRITE = 6;

    SpriteManager(const sf::Vector2u& atlasSize, unsigned padding)
        : mVertices(sf::PrimitiveType::Triangles),
          mNumSprites{0},
          mAtlas(atlasSize, padding),
          mShader(std::filesystem::path{"main.vert"}, std::filesystem::path{"main.frag"}) {
        const GLuint program = mShader.getNativeHandle();

        // vertex attribute locations
        mPositionAttribLocation = glGetAttribLocation(program, "position");
        glCheck();
        mTexCoordsAttribLocation = glGetAttribLocation(program, "texCoords");
        glCheck();
        mSpriteNumberAttribLocation = glGetAttribLocation(program, "spriteNumber");
        glCheck();

        // uniform locations
        mTextureUniformLocation = glGetUniformLocation(program, "texture");
        glCheck();
        mTransformsUniformLocation = glGetUniformLocation(program, "transforms");
        glCheck();
        mNumTransformsUniformLocation = glGetUniformLocation(program, "numTransforms");
        glCheck();

        // GPU-side buffers / textures
        glCheck(glGenBuffers(1, &mVerticesVBO));
        glCheck(glGenBuffers(1, &mSpriteNumberVBO));
        glCheck(glGenTextures(1, &mTransformsTexture));
    }

    ~SpriteManager() {
        glDeleteBuffers(1, &mVerticesVBO);
        glDeleteBuffers(1, &mSpriteNumberVBO);
        glDeleteTextures(1, &mTransformsTexture);
    }

    
    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    TextureID registerTexture(const std::string& filepath) {
        return mAtlas.registerTexture(filepath);
    }

    SpriteID createSprite(TextureID textureID) {
        SpriteID id = mNumSprites++;
        for (size_t i = 0; i < VERTICES_PER_SPRITE; ++i) {
            mVertices.append(sf::Vertex{});
            mSpriteNumbers.push_back(static_cast<float>(id));
        }
        mTransforms.emplace_back();
        writeQuad(id, textureID);
        return id;
    }

    void setTexture(SpriteID sprite, TextureID newTexture) {
        assert(sprite < mNumSprites);
        writeQuad(sprite, newTexture);
    }

    // Batched rendering (part 1): transforms applied on the CPU, single SFML draw call.
    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        sf::VertexArray transformed = mVertices;
        for (size_t s = 0; s < mNumSprites; ++s) {
            const BasicTransform& t = mTransforms[s];
            for (size_t v = s * VERTICES_PER_SPRITE; v < (s + 1) * VERTICES_PER_SPRITE; ++v) {
                transformed[v].position = t.applyPoint(transformed[v].position);
            }
        }
        states.texture = &mAtlas.getTexture();
        states.shader = &mShader;
        target.draw(transformed, states);
    }

    // Batched rendering (part 2): transforms applied in parallel on the GPU in the vertex shader.
    void drawGL() {
        if (mNumSprites == 0) {
            return;
        }
        glCheck(glUseProgram(mShader.getNativeHandle()));

        // vertex positions + uv coords (interleaved sf::Vertex)
        static_assert(sizeof(sf::Vertex) == 20);
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mVerticesVBO));
        glCheck(glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(mVertices.getVertexCount() * sizeof(sf::Vertex)),
                             &mVertices[0], GL_STREAM_DRAW));
        if (mPositionAttribLocation >= 0) {
            glCheck(glVertexAttribPointer(static_cast<GLuint>(mPositionAttribLocation), 2, GL_FLOAT,
                                          GL_FALSE, sizeof(sf::Vertex),
                                          (const void*)offsetof(sf::Vertex, position)));
            glCheck(glEnableVertexAttribArray(static_cast<GLuint>(mPositionAttribLocation)));
        }
        if (mTexCoordsAttribLocation >= 0) {
            glCheck(glVertexAttribPointer(static_cast<GLuint>(mTexCoordsAttribLocation), 2, GL_FLOAT,
                                          GL_FALSE, sizeof(sf::Vertex),
                                          (const void*)offsetof(sf::Vertex, texCoords)));
            glCheck(glEnableVertexAttribArray(static_cast<GLuint>(mTexCoordsAttribLocation)));
        }

        // sprite numbers (separate buffer ---
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mSpriteNumberVBO));
        glCheck(glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(mSpriteNumbers.size() * sizeof(float)),
                             mSpriteNumbers.data(), GL_STREAM_DRAW));
        if (mSpriteNumberAttribLocation >= 0) {
            glCheck(glVertexAttribPointer(static_cast<GLuint>(mSpriteNumberAttribLocation), 1,
                                          GL_FLOAT, GL_FALSE, sizeof(float), (const void*)0));
            glCheck(glEnableVertexAttribArray(static_cast<GLuint>(mSpriteNumberAttribLocation)));
        }
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));

        // atlas texture on texture unit 0
        glCheck(glActiveTexture(GL_TEXTURE0));
        glCheck(glBindTexture(GL_TEXTURE_2D, mAtlas.getTexture().getNativeHandle()));

        // transforms as a 1D RGBA32F texture on texture unit 1

        static_assert(sizeof(BasicTransform) == 4 * sizeof(float));
        glCheck(glActiveTexture(GL_TEXTURE1));
        glCheck(glBindTexture(GL_TEXTURE_1D, mTransformsTexture));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        glCheck(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F_ARB, static_cast<GLsizei>(mNumSprites),
                             0, GL_RGBA, GL_FLOAT, mTransforms.data()));
        glCheck(glActiveTexture(GL_TEXTURE0));

        // uniforms
        glCheck(glUniform1i(mTextureUniformLocation, 0));
        glCheck(glUniform1i(mTransformsUniformLocation, 1));
        glCheck(glUniform1i(mNumTransformsUniformLocation, static_cast<GLint>(mNumSprites)));

        // single draw call for every sprite
        glCheck(glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.getVertexCount())));
    }

    BasicTransform& getTransform(SpriteID id) {
        assert(id < mTransforms.size());
        return mTransforms[id];
    }

    const TextureAtlas& getAtlas() const { return mAtlas; }

private:
    void writeQuad(SpriteID sprite, TextureID tex) {
        const sf::Vector2f size(mAtlas.getSizePixels(tex));
        const sf::FloatRect uv = mAtlas.getBoundsUV(tex);
        const float u0 = uv.position.x, u1 = uv.position.x + uv.size.x;
        const float v0 = uv.position.y, v1 = uv.position.y + uv.size.y;  // v0 bottom, v1 top

        const sf::Vertex bl{{0.f, 0.f}, sf::Color::White, {u0, v0}};
        const sf::Vertex br{{size.x, 0.f}, sf::Color::White, {u1, v0}};
        const sf::Vertex tr{{size.x, size.y}, sf::Color::White, {u1, v1}};
        const sf::Vertex tl{{0.f, size.y}, sf::Color::White, {u0, v1}};

        const size_t base = sprite * VERTICES_PER_SPRITE;
        mVertices[base + 0] = bl;
        mVertices[base + 1] = br;
        mVertices[base + 2] = tr;
        mVertices[base + 3] = bl;
        mVertices[base + 4] = tr;
        mVertices[base + 5] = tl;
    }

    sf::VertexArray mVertices;
    std::vector<BasicTransform> mTransforms;
    size_t mNumSprites;
    TextureAtlas mAtlas;
    mutable sf::Shader mShader;

    // Use the below only in Batched rendering (part 2):
    std::vector<float> mSpriteNumbers;

    GLint mTextureUniformLocation;
    GLint mTransformsUniformLocation;
    GLint mNumTransformsUniformLocation;

    GLint mPositionAttribLocation;
    GLint mTexCoordsAttribLocation;
    GLint mSpriteNumberAttribLocation;

    GLuint mVerticesVBO;
    GLuint mSpriteNumberVBO;
    GLuint mTransformsTexture;
};

#endif  // SPRITE_MANAGER_H
