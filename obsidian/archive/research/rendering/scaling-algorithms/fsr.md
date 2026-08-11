# AMD FidelityFX Super Resolution (FSR)

> Gaming-optimized upscaling with edge reconstruction.

## Overview

FSR is a two-pass spatial upscaler using modified Lanczos filtering. Designed for real-time gaming with minimal overhead.

**Best for:** Gaming, real-time applications, performance-conscious upscaling

---

## Architecture

Two-pass approach:

### Pass 1: EASU (Edge Adaptive Spatial Upsampling)
- Adaptive elliptical filter
- Edge detection for direction-aware sampling
- Lanczos-based kernel with ringing reduction

### Pass 2: RCAS (Robust Contrast Adaptive Sharpening)
- Contrast-adaptive sharpening
- Removes softening from upscaling

---

## GLSL Implementation

```glsl
// FSR EASU Pass (simplified)
// Based on AMD GPUOpen reference

// Input callbacks for textureGather
vec4 FsrEasuRF(vec2 p) { return textureGather(Source, p, 0); }
vec4 FsrEasuGF(vec2 p) { return textureGather(Source, p, 1); }
vec4 FsrEasuBF(vec2 p) { return textureGather(Source, p, 2); }

void main() {
    vec2 uv = gl_FragCoord.xy / output_size;
    
    // EASU parameters
    vec4 con0, con1, con2, con3;
    FsrEasuConst(con0, con1, con2, con3, 
                  input_size, input_size, 
                  input_size, output_size);
    
    vec3 color;
    FsrEasuF(color, uv, con0, con1, con2, con3);
    
    FragColor = vec4(color, 1.0);
}
```

---

## wlroots Integration

```c
// Pseudo-code for FSR integration
struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(
    renderer, buffer, NULL
);

// First pass: EASU upscaling
wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
    .texture = source,
    .src_box = {0, 0, src_width, src_height},
    .dst_box = {0, 0, dst_width, dst_height},
    .custom_shader = fsr_easu_shader,
});

// Second pass: RCAS sharpening
wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
    .texture = intermediate,
    .custom_shader = fsr_rcas_shader,
});
```

---

## Quality Modes

| Mode | Input | Output | Quality | GPU Cost |
|------|-------|--------|---------|----------|
| Ultra Quality | 1.3x down | Native | 5/5 | ~20% |
| Quality | 1.5x down | Native | 4/5 | ~30% |
| Balanced | 1.7x down | Native | 3/5 | ~40% |
| Performance | 2.0x down | Native | 3/5 | ~50% |

---

## Characteristics

| Aspect | Value |
|--------|-------|
| Passes | 2 |
| Edge Adaptation | Yes |
| Sharpness Control | RCAS parameters |
| GPU Vendor | AMD (works everywhere) |
| Open Source | Yes (MIT license) |

---

## When to Use

| Scenario | Recommendation |
|----------|----------------|
| Gaming at 1.5x+ | Yes Excellent quality/perf |
| Real-time video | Yes Good balance |
| Static images | Caution: Lanczos better |
| Text rendering | Caution: May oversharpen |

---

## Related

- [NVIDIA NIS](./nis.md) — Alternative single-pass
- [Lanczos](./lanczos.md) — Higher static quality
- [Performance Comparison](./performance.md) — Benchmarks
