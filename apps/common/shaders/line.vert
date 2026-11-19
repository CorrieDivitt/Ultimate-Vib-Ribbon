#version 450
// One instance per line segment; six vertices expand it to a screen-space quad
// of constant pixel width, so lines stay crisp at any distance or resolution.

layout(location = 0) in vec3 in_a;
layout(location = 1) in vec3 in_b;
layout(location = 2) in vec4 in_colour;

layout(push_constant) uniform Push {
  mat4 mvp;
  vec2 viewport;   // pixels
  float width;     // line width in pixels
  float feather;   // anti-aliasing band in pixels
} pc;

layout(location = 0) out vec4 v_colour;
layout(location = 1) out float v_side;   // -1..1 across the line, in units of half_total
layout(location = 2) out float v_half_total;

const float kNear = 0.0001;

void main() {
  vec4 a = pc.mvp * vec4(in_a, 1.0);
  vec4 b = pc.mvp * vec4(in_b, 1.0);

  // Clip the segment against the near plane so projection stays valid.
  if (a.w < kNear && b.w < kNear) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);  // outside the view volume
    v_colour = vec4(0.0); v_side = 0.0; v_half_total = 1.0;
    return;
  }
  if (a.w < kNear) a = mix(a, b, (kNear - a.w) / (b.w - a.w));
  if (b.w < kNear) b = mix(b, a, (kNear - b.w) / (a.w - b.w));

  // Corner layout: 0,1,2 and 2,1,3 where bit 0 picks the end and bit 1 the side.
  const int corner[6] = int[6](0, 1, 2, 2, 1, 3);
  int c = corner[gl_VertexIndex];
  bool at_b = (c & 1) != 0;
  float side = (c & 2) != 0 ? 1.0 : -1.0;

  vec2 sa = (a.xy / a.w) * 0.5 * pc.viewport;
  vec2 sb = (b.xy / b.w) * 0.5 * pc.viewport;
  vec2 dir = sb - sa;
  float len = length(dir);
  dir = len > 0.0001 ? dir / len : vec2(1.0, 0.0);
  vec2 normal = vec2(-dir.y, dir.x);

  float half_total = 0.5 * pc.width + pc.feather;
  vec4 p = at_b ? b : a;
  vec2 s = (at_b ? sb : sa) + normal * side * half_total + dir * (at_b ? half_total : -half_total);
  vec2 ndc = s / (0.5 * pc.viewport);

  // Pull lines slightly toward the camera so they win against the fills they outline.
  gl_Position = vec4(ndc * p.w, p.z - 0.0005 * p.w, p.w);
  v_colour = in_colour;
  v_side = side;
  v_half_total = half_total;
}
