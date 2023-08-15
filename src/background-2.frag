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

mat2 r2d (in float degree)
{
	float rad = radians (degree);
	float c = cos (rad);
	float s = sin (rad);

	return mat2 (vec2 (c, s), vec2 (-s, c));
}

float line (in vec2 a, in vec2 b)
{
	vec2 ba = b - a;
	float h = clamp (dot (-a, ba)/dot (ba, ba), .0, 1.);

	return length (-a - ba*h);
}

float grid (in vec2 p)
{
	p += 2.*vec2 (cos(time),sin(time));
	p *= 2. + 2.*(.5 + .5*cos (time));
	p *= r2d (37.*time);

	vec2 s = floor(p + (p.x + p.y)*.36602540378);
	p -= s - (s.x + s.y)*.211324865;
	vec2 v0 = p;
	float i = v0.x < v0.y ? 1. : 0.; 
	vec2 ioffs = vec2(1. - i, i); 
	vec2 v1 = v0 - ioffs + .2113248654;
	vec2 v2 = v0 - .577350269;
	vec2 center = (v0 + v1 + v2)/3.;
	vec2 e0 = .5*(v0 + v1);
	vec2 e1 = .5*(v1 + v2);
	vec2 e2 = .5*(v2 + v0);

	float t = 4.*time;
	center *= r2d (45.*sin(t));
	vec2 offset = .035*vec2 (cos(t), sin(t));
	float l0 = line (center + offset, e0);
	float l1 = line (center + offset, e1);
	float l2 = line (center + offset, e2);

	float m0 = smoothstep (.02, .01, l0);
	float m1 = smoothstep (.02, .01, l1);
	float m2 = smoothstep (.02, .01, l2);

	return m0 + m1 + m2;
}

void main()
{
	vec2 uv = fragCoord.yx;
	vec2 uvRaw = uv;
	uv = uv.yx * 2. - 1.;
	uv.y *= resolution.y/resolution.x;
	uv *= 1. + .5*length(fragCoord.yx*2. - 1.);

	float offsetScale = .0175;
	vec2 offsetGreen = vec2(offsetScale*length(fragCoord.xy*2. - 1.));
	vec2 offsetBlue = vec2(-offsetScale*length(fragCoord.xy*2. - 1.));

	vec3 col = vec3 (1., .95, .9);
	float layerLarge = 3.*grid(uv);
	float layerMedium = .75*grid(2.*uv*r2d(3.));
	float layerSmall = .25*grid(3.*uv*r2d(6.));
	float layerTiny = .125*grid(3.*uv*r2d(9.));

	vec2 uvGreen = uv + offsetGreen;
	float layerLargeGreen = 3.*grid(uvGreen);
	float layerMediumGreen = .75*grid(2.*uvGreen*r2d(3.));
	float layerSmallGreen = .25*grid(3.*uvGreen*r2d(6.));
	float layerTinyGreen = .125*grid(3.*uvGreen*r2d(9.));

	vec2 uvBlue = uv + offsetBlue;
	float layerLargeBlue = 3.*grid(uvBlue);
	float layerMediumBlue = .75*grid(2.*uvBlue*r2d(3.));
	float layerSmallBlue = .25*grid(3.*uvBlue*r2d(6.));
	float layerTinyBlue = .125*grid(3.*uvBlue*r2d(9.));

	// fake chromatic aberration
	col.r += 4.*(layerLarge + layerMedium + layerSmall + layerTiny);
	col.g += 4.*(layerLargeGreen + layerMediumGreen + layerSmallGreen + layerTinyGreen);
	col.b += 4.*(layerLargeBlue + layerMediumBlue + layerSmallBlue + layerTinyBlue);

	col *= 1. - .675*length (fragCoord.xy*2. - 1.);
	col = col / (1. + col);
	col *= mix (1., .5, .5 + .5*cos (900.*uvRaw.y));
	col = pow (col, vec3 (1./2.2));

	gl_FragColor = vec4 (col*.4, 1.);
}
