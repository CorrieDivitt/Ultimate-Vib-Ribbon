#version 450
// Solid fills: the small black polygons that hide lines behind a character.

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_colour;

layout(push_constant) uniform Push {
  mat4 mvp;
  vec2 viewport;
  float width;
  float feather;
} pc;

layout(location = 0) out vec4 v_colour;

void main() {
  gl_Position = pc.mvp * vec4(in_position, 1.0);
  v_colour = in_colour;
}
