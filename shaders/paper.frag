/* Paper mode ("reading" tint): remap the composited window texture toward a
 * warm paper page. Bright near-neutral pixels (page background) become warm
 * paper; dark pixels (text) stay dark, tinted toward warm ink; saturated
 * content keeps its hue so images/syntax colours survive. Per-window effect —
 * runs in screen space after the scene transform, so zoom is free.
 * GLES2 / GLSL ES 1.00, matching passthrough.frag. */
precision mediump float;

uniform sampler2D tex;      /* the composited window/scene */
varying vec2 v_uv;

uniform float u_strength;   /* 0 = passthrough, 1 = full paper           */
uniform vec3  u_paper;      /* colour a pure-white page maps to          */
uniform vec3  u_ink;        /* colour pure black maps to                 */
uniform float u_preserve;   /* how strongly saturated pixels keep hue    */

void main() {
	vec3 c = texture2D(tex, v_uv).rgb;

	float lum   = dot(c, vec3(0.299, 0.587, 0.114));
	float maxc  = max(c.r, max(c.g, c.b));
	float minc  = min(c.r, min(c.g, c.b));
	float chroma = maxc - minc;

	/* Duotone spine: black->ink, white->paper, gamma-eased so mid greys
	 * (borders, code bg) land on a believable page tone. */
	float t = pow(lum, 0.9);
	vec3 duotone = mix(u_ink, u_paper, t);

	/* Colourful pixels (icons, images, syntax) keep their own hue but get a
	 * gentle warm wash from the paper so nothing looks pasted-on. */
	float colorful = smoothstep(0.10, 0.32, chroma) * u_preserve;
	vec3 warmed = c * mix(vec3(1.0), u_paper / max(vec3(0.75), u_paper), 0.35);
	vec3 paper = mix(duotone, warmed, colorful);

	gl_FragColor = vec4(mix(c, paper, u_strength), 1.0);
}
