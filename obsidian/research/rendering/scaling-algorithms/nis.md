# NVIDIA Image Scaling (NIS)

> Single-pass upscaling with adaptive sharpening.

## Overview

NIS is a 6-tap adaptive filter with sharpening. Simpler than FSR (single pass) but effective for real-time upscaling.

**Best for:** Gaming, NVIDIA GPUs, simpler integration

---

## Architecture

- **6-tap adaptive filter** with sharpening
- **Directionally adaptive** edge enhancement
- **Single pass** (vs FSR's two passes)

---

## Key Differences from FSR

| Aspect | FSR | NIS |
|--------|-----|-----|
| Passes | 2 | 1 |
| Edge Adaptation | Yes | Limited |
| Sharpness Control | RCAS parameters | Global intensity |
| GPU Vendor | AMD (universal) | NVIDIA (universal) |
| Complexity | Higher | Lower |

---

## Implementation

```glsl
// NIS single-pass shader (simplified)
uniform sampler2D source;
uniform vec2 input_size;
uniform vec2 output_size;
uniform float sharpness; // 0.0 to 1.0

void main() {
    vec2 uv = gl_FragCoord.xy / output_size;
    vec2 texel = 1.0 / input_size;
    
    // 6-tap sampling with edge detection
    vec3 center = texture(source, uv).rgb;
    vec3 left   = texture(source, uv - vec2(texel.x, 0.0)).rgb;
    vec3 right  = texture(source, uv + vec2(texel.x, 0.0)).rgb;
    vec3 up     = texture(source, uv - vec2(0.0, texel.y)).rgb;
    vec3 down   = texture(source, uv + vec2(0.0, texel.y)).rgb;
    
    // Edge-adaptive weights
    float edge_h = length(left - right);
    float edge_v = length(up - down);
    
    // Adaptive filter
    vec3 result = center * 0.5 + (left + right) * 0.125 + (up + down) * 0.125;
    
    // Apply sharpening
    vec3 sharpened = center + (center - result) * sharpness;
    
    FragColor = vec4(sharpened, 1.0);
}
```

---

## Characteristics

| Aspect | Value |
|--------|-------|
| Memory Fetches | 5 per sample |
| Performance | 4/5 Fast |
| Upscale Quality | 4/5 Very good |
| Integration Complexity | Low |
| Sharpness Control | Single parameter |

---

## When to Use

| Scenario | Recommendation |
|----------|----------------|
| Quick integration | Yes Single pass is simpler |
| Performance critical | Yes Lower overhead than FSR |
| Maximum quality | Caution: FSR has better edge reconstruction |
| Non-gaming content | Caution: Lanczos may be better |

---

## Related

- [AMD FSR](./fsr.md) — Higher quality alternative
- [Lanczos](./lanczos.md) — Best static quality
- [Bilinear](./bilinear.md) — Faster default
