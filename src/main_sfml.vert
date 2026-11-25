#version 120

varying vec2 texCoordsOut;

// Given a world position (for this lab, bottom-left is (0, 0), top-right of window is (1280, 720)),
// convert it to be in [-1, 1]^2, where y = -1 is the bottom and x = -1 is the left side.
vec2 cameraPos(vec2 worldPos) {
    return worldPos / vec2(1280.0, 720.0) * 2.0 - 1.0;
}

void main() {
    // output to gl_Position and to texCoordsOut.
    gl_Position = vec4(cameraPos(gl_Vertex.xy), 0.0, 1.0);
    // Our UVs have their origin at the bottom-left, but sf::Texture stores its rows top-to-bottom
    texCoordsOut = vec2(gl_MultiTexCoord0.x, 1.0 - gl_MultiTexCoord0.y);
}
