#version 450

layout(location = 0) in vec4 v_colour;
layout(location = 1) in float v_side;
layout(location = 2) in float v_half_total;

layout(push_constant) uniform Push {
  mat4 mvp;
  vec2 viewport;
  float width;
  float feather;
} pc;

layout(location = 0) out vec4 out_colour;

void main() {
  float dist = abs(v_side) * v_half_total;           // pixels from the line's centre
  float alpha = 1.0 - smoothstep(0.5 * pc.width - pc.feather, 0.5 * pc.width + pc.feather, dist);
  out_colour = vec4(v_colour.rgb, v_colour.a * alpha);
}
