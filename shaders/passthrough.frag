/* Phase 0 passthrough: sample the offscreen scene texture unchanged. Enabling
 * the output pass with this shader must be visually byte-identical to a plain
 * wlr_scene_output_commit() — it is the proof that the offscreen-render +
 * fragment-pass plumbing is correct before any real effect is written.
 * GLES2 / GLSL ES 1.00. */
precision mediump float;

uniform sampler2D tex; /* the composited scene */
varying vec2 v_uv;

void main() {
	gl_FragColor = texture2D(tex, v_uv);
}
