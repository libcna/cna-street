#version 450
// The fullscreen pass's vertex program on Vulkan: SpriteBatch's attribute layout, and the viewport
// size in the push-constant block the renderer gives every sprite-path ShaderEffect draw.

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec4 SpriteColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
} pc;

void main()
{
    vec2 ndc = (aPos / pc.viewportSize) * 2.0 - vec2(1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
}
