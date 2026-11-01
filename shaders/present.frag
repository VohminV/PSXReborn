#version 450
layout(constant_id = 0) const bool IS_4K = false;
layout(constant_id = 1) const bool ENABLE_OVERLAY = true;

layout(set = 0, binding = 0) uniform sampler2D gameTexture;
layout(set = 0, binding = 1) uniform sampler2D overlayTexture;

layout(push_constant) uniform Constants {
    vec4 sourceRect;
    vec4 destinationRect;
    uvec4 flags;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 position = gl_FragCoord.xy;
    vec3 color = vec3(0.0);
    if (position.x >= pc.destinationRect.x &&
        position.y >= pc.destinationRect.y &&
        position.x < pc.destinationRect.x + pc.destinationRect.z &&
        position.y < pc.destinationRect.y + pc.destinationRect.w) {
        vec2 uv = (position - pc.destinationRect.xy) / pc.destinationRect.zw;
        uv = mix(pc.sourceRect.xy, pc.sourceRect.zw, uv);
        color = texture(gameTexture, uv).rgb;
    }
    if (ENABLE_OVERLAY && pc.flags.x != 0u) {
        vec2 size = vec2(textureSize(overlayTexture, 0));
        vec2 uv = position / size;
        if (IS_4K) {
            uv = (floor(uv * size) + vec2(0.5)) / size;
        }
        vec4 overlay = texture(overlayTexture, uv);
        color = mix(color, overlay.rgb, overlay.a);
    }
    outColor = vec4(color, 1.0);
}
