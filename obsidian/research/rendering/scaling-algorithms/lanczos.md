# Lanczos Resampling

> High-quality sinc-based scaling algorithm.

## Overview

Lanczos resampling uses a windowed sinc function to achieve high-quality upscaling with reduced aliasing compared to bilinear filtering.

**Best for:** Photos, video content, quality-critical applications

---

## Mathematical Foundation

The Lanczos kernel is a windowed sinc function:

```
L(x) = { sinc(x) * sinc(x/a)  if |x| < a
       { 0                    otherwise

where sinc(x) = sin(πx) / (πx)
      a = number of lobes (typically 2 or 3)
```

---

## GPU Implementation

### GLSL Fragment Shader

```glsl
// Optimized Lanczos-2 for fragment shader
// Precomputed weights for 4 taps each direction

const float LANCZOS_WEIGHTS[4] = float[](
    0.38026,   // center
    0.27667,   // +/-1
    0.08074,   // +/-2
    -0.02143   // +/-3 (negative weight for ringing)
);

float lanczos_weight(float x) {
    // Lanczos-2: a = 2
    const float a = 2.0;
    if (abs(x) < 0.001) return 1.0;
    if (abs(x) > a) return 0.0;
    
    float pix = 3.14159265359 * x;
    return (sin(pix) / pix) * (sin(pix / a) / (pix / a));
}

vec4 lanczos_sample(sampler2D tex, vec2 uv, vec2 texel_size) {
    vec2 pos = uv / texel_size;
    vec2 f = fract(pos);
    vec2 base = floor(pos) * texel_size + texel_size * 0.5;
    
    vec4 result = vec4(0.0);
    float weight_sum = 0.0;
    
    for (int x = -3; x <= 3; x++) {
        for (int y = -3; y <= 3; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            float dist = length(vec2(float(x), float(y)) - f);
            float w = lanczos_weight(dist);
            result += texture(tex, base + offset) * w;
            weight_sum += w;
        }
    }
    
    return result / weight_sum;
}
```

### Separable Optimization

Perform horizontal then vertical passes to reduce complexity from O(n²) to O(2n):

```glsl
// Horizontal pass
vec4 lanczos_h(sampler2D tex, vec2 uv, vec2 texel_size) {
    // Sample 7 horizontal taps
    // Output to intermediate texture
}

// Vertical pass  
vec4 lanczos_v(sampler2D tex, vec2 uv, vec2 texel_size) {
    // Sample 7 vertical taps from intermediate
}
```

---

## Variants

| Variant | Taps | Quality | Use Case |
|---------|------|---------|----------|
| Lanczos-2 | 4 | Good | Real-time, balanced |
| Lanczos-3 | 6 | Better | Quality-focused |
| EWA Lanczos (Jinc) | Variable | Best | Still images, non-realtime |

---

## Characteristics

| Aspect | Value |
|--------|-------|
| Memory Fetches | 16-36 per sample |
| Performance | 3/5 Medium |
| Upscale Quality | 5/5 Excellent |
| Downscale Quality | 4/5 Very good |
| Sharpness | High (may ring) |

---

## When to Use

| Scenario | Recommendation |
|----------|----------------|
| Photos | Yes Excellent preservation |
| Video playback | Yes Great quality |
| Real-time gaming | Caution: May be too slow |
| Text/UI | Caution: Can cause ringing |
| Pixel art | No Use nearest |

---

## Related

- [Bilinear](./bilinear.md) — Faster alternative
- [AMD FSR](./fsr.md) — Gaming-optimized
- [Performance Comparison](./performance.md) — Benchmarks
