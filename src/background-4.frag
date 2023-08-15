/*
 * kmscon - Fragment Shader
 *
 * Copyright (c) 2011-2012 David Herrmann <dh.herrmann@googlemail.com>
 * Copyright (c) 2011 University of Tuebingen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Fragment Shader
 * A basic fragment shader for drawing the background.
 */

precision highp float;

uniform vec2 resolution;
uniform float time;

varying vec2 fragCoord;

const vec3 red = vec3 (1.0, 0.0, 0.0);
const vec3 green = vec3 (0.0, 1.0, 0.0);
const vec3 blue = vec3 (0.0, 0.0, 1.0);
const vec3 white = vec3 (1.0);
const vec3 orange = vec3 (1.0, 0.4, 0.125);
const vec3 black = vec3 (0.2, 0.3, 0.2);
const vec3 cyan = vec3 (0.0, 1.0, 1.0);
const vec3 magenta = vec3 (1.0, 0.0, 1.0);
const vec3 yellow = vec3 (1.0, 1.0, 0.0);
const float SIZE = .003;

float distLine (vec2 p, vec2 a, vec2 b)
{
	vec2 pa = p - a;
	vec2 ba = b - a;
	float t = clamp ( dot (pa, ba) / dot (ba, ba), .0, 1.);
	return length (pa - ba*t);
}

float lineMask (vec2 uv, vec2 a, vec2 b)
{
	float d = distLine (uv, a, b);
	float thickness = SIZE;
	return smoothstep (thickness, .125*thickness, d);
}

vec3 glowLine (vec2 uv, vec2 a, vec2 b, vec3 rgbGlow)
{
	float m = lineMask (uv, a, b);
	float dist = distLine (uv, a, b);
	float brightness = SIZE/pow (.085 + 2.*dist, 2.);
	vec3 color = m*vec3 (.7);
	color += rgbGlow*brightness;
	return color;
}

struct boxType {vec4 p[8];};

mat4 trans (vec3 t)
{
	mat4 mat = mat4 (vec4 (1., .0, .0, .0),
					 vec4 (.0, 1., .0, .0),
					 vec4 (.0, .0, 1., .0),
					 vec4 (t.x, t.y, t.z, 1.));
	return mat;
}

mat4 rotX (float angle)
{
	float rad = radians (angle);
	float c = cos (rad);
	float s = sin (rad);

	mat4 mat = mat4 (vec4 (1., .0, .0, .0),
					 vec4 (.0,   c,   s, .0),
					 vec4 (.0,  -s,   c, .0),
					 vec4 (.0, .0, .0, 1.));

	return mat;
}

mat4 rotY (float angle)
{
	float rad = radians (angle);
	float c = cos (rad);
	float s = sin (rad);

	mat4 mat = mat4 (vec4 (  c, .0,  -s, .0),
					 vec4 (.0, 1., .0, .0),
					 vec4 (  s, .0,   c, .0),
					 vec4 (.0, .0, .0, 1.));

	return mat;
}

mat4 rotZ (float angle)
{
	float rad = radians (angle);
	float c = cos (rad);
	float s = sin (rad);

	mat4 mat = mat4 (vec4 (  c,   s, .0, .0),
					 vec4 ( -s,   c, .0, .0),
					 vec4 (.0, .0, 1.0, .0),
					 vec4 (.0, .0, .0, 1.));

	return mat;
}

void apply (inout boxType box, mat4 model)
{
	box.p[0] = model*box.p[0];
	box.p[1] = model*box.p[1];
	box.p[2] = model*box.p[2];
	box.p[3] = model*box.p[3];
	box.p[4] = model*box.p[4];
	box.p[5] = model*box.p[5];
	box.p[6] = model*box.p[6];
	box.p[7] = model*box.p[7];
}

vec3 drawCube (vec2 uv, boxType box, float scale)
{
	vec3 col = glowLine (uv, scale*box.p[0].xy/box.p[0].z, scale*box.p[1].xy/box.p[1].z, red);
	col += glowLine (uv, scale*box.p[1].xy/box.p[1].z, scale*box.p[2].xy/box.p[2].z, green);
	col += glowLine (uv, scale*box.p[2].xy/box.p[2].z, scale*box.p[3].xy/box.p[3].z, orange);
	col += glowLine (uv, scale*box.p[3].xy/box.p[3].z, scale*box.p[0].xy/box.p[0].z, cyan);
	col += glowLine (uv, scale*box.p[4].xy/box.p[4].z, scale*box.p[5].xy/box.p[5].z, blue);
	col += glowLine (uv, scale*box.p[5].xy/box.p[5].z, scale*box.p[6].xy/box.p[6].z, red);
	col += glowLine (uv, scale*box.p[6].xy/box.p[6].z, scale*box.p[7].xy/box.p[7].z, yellow);
	col += glowLine (uv, scale*box.p[7].xy/box.p[7].z, scale*box.p[4].xy/box.p[4].z, green);
	col += glowLine (uv, scale*box.p[0].xy/box.p[0].z, scale*box.p[4].xy/box.p[4].z, blue);
	col += glowLine (uv, scale*box.p[1].xy/box.p[1].z, scale*box.p[5].xy/box.p[5].z, cyan);
	col += glowLine (uv, scale*box.p[2].xy/box.p[2].z, scale*box.p[6].xy/box.p[6].z, green);
	col += glowLine (uv, scale*box.p[3].xy/box.p[3].z, scale*box.p[7].xy/box.p[7].z, magenta);

	return col;
}

void main()
{
	vec2 uv = fragCoord.yx* 2. - 1.;
	uv.x *= resolution.y/resolution.x;
	uv *= 1. + .4*length(uv);
	uv.x += .0035*cos(40.*uv.x + 2.*time);

	boxType box;
	box.p[0] = vec4 ( 0.1,  0.1,  0.1, 1.0);
	box.p[1] = vec4 ( 0.1, -0.1,  0.1, 1.0);
	box.p[2] = vec4 (-0.1, -0.1,  0.1, 1.0);
	box.p[3] = vec4 (-0.1,  0.1,  0.1, 1.0);
	box.p[4] = vec4 ( 0.1,  0.1, -0.1, 1.0);
	box.p[5] = vec4 ( 0.1, -0.1, -0.1, 1.0);
	box.p[6] = vec4 (-0.1, -0.1, -0.1, 1.0);
	box.p[7] = vec4 (-0.1,  0.1, -0.1, 1.0);

	boxType box2 = box;
	boxType box3 = box;
	boxType box4 = box;

	float t = 8. + 14.*time;
	mat4 rot3d = rotX (-4.*t)*rotY (3.*t)*rotZ (2.*t);
	mat4 model = trans (vec3 (.0, .0, -.275))*rot3d;
	apply (box, model);
	vec3 boxCol = .5*drawCube (uv, box, 1.);

	mat4 rot3d2 = rotX (-4.025*t)*rotY (3.025*t)*rotZ (2.025*t);
	mat4 model2 = trans (vec3 (.0, .0, -.275))*rot3d2;
	apply (box2, model2);
	boxCol += .5*drawCube (uv, box2, .8);

	mat4 rot3d3 = rotX (-4.05*t)*rotY (3.05*t)*rotZ (2.05*t);
	mat4 model3 = trans (vec3 (.0, .0, -.275))*rot3d3;
	apply (box3, model3);
	boxCol += .5*drawCube (uv, box3, .6);

	mat4 rot3d4 = rotX (-4.075*t)*rotY (3.075*t)*rotZ (2.075*t);
	mat4 model4 = trans (vec3 (.0, .0, -.275))*rot3d4;
	apply (box4, model4);
	boxCol += .5*drawCube (uv, box4, .4);

	boxCol = boxCol / (1. + boxCol);
	boxCol = sqrt (boxCol);
	boxCol *= mix (1., .5, .5 + .5*cos (1300.*uv.y));

	gl_FragColor = vec4(boxCol*.55, 1.);
}
