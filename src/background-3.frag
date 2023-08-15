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

const float EPSILON = .001;
const int MAX_ITER = 48; 
const float STEP_BIAS = 1.1;

mat2 r2d (in float a)
{
	float c = cos (radians (a));
	float s = sin (radians (a));

	return mat2 (vec2(c, s), vec2(-s, c));
}

float opCombine (in float d1, in float d2, in float r)
{
	float h = clamp (.5 + .5 * (d2 - d1) / r, .0, 1.);

	return mix (d2, d1, h) - r * h * (1. - h); 
}

float sdSphere (in vec3 p, in float r)
{
	return length (p) - r;
}

vec2 hash (in vec2 p)
{
	p = vec2 (dot (p, vec2 (127.1, 311.7)),
			  dot (p, vec2 (269.5, 183.3)));

	return -1. + 2.*fract (sin (p)*43758.5453123);
}

float noise (in vec2 p)
{
	const float K1 = .366025404;
	const float K2 = .211324865;

	vec2 i = floor (p + (p.x + p.y)*K1);

	vec2 a = p - i + (i.x + i.y)*K2;
	vec2 o = step (a.yx, a.xy);    
	vec2 b = a - o + K2; 
	vec2 c = a - 1. + 2.*K2;

	vec3 h = max (.5 - vec3 (dot (a, a), dot (b, b), dot (c, c) ), .0);

	vec3 n = h*h*h*h*vec3 (dot (a, hash (i + .0)),
						   dot (b, hash (i + o)),
						   dot (c, hash (i + 1.)));

	return dot (n, vec3 (70.));
}

float fbm (in vec2 p, in int iters)
{
	mat2 rot = r2d (35.);
	float d = .0;
	float f = 1.;
	float fsum = .0;

	for (int i = 0; i < iters; ++i) {
		d += f*noise (p);
		p *= rot;
		fsum += f;
		f *= .5;
	}
	d /= fsum;

	return d;
}

float scene (in vec3 p)
{
	float t = 2.*time;
	vec3 pBottom = p;
	pBottom.x += 3.*time;
	float bottom = pBottom.y + 1. + .25*fbm(pBottom.xz, 5);

	float t4 = 2. * time;
	float t5 = 2.5 * time;
	float t6 = 1.75 * time;
	float t7 = 2.5 * time;
	float r1 = .1 + .3 * (.5 + .5 * sin (2.*t4));
	float r2 = .1 + .25 * (.5 + .5 * sin (3.*t5));
	float r3 = .1 + .3 * (.5 + .5 * sin (4.*t6));
	float r4 = .1 + .25 * (.5 + .5 * sin (5.*t7));

	float t1 = 1.5 * time;
	float t2 = 2. * time;
	float t3 = 2.5 * time;
	vec3 offset1 = vec3 (-.1*cos(t1), .1, -.2*sin(t2));
	vec3 offset2 = vec3 (.2, .2*cos(t2), .3*sin(t3));
	vec3 offset3 = vec3 (-.2*cos(t3), -.2*sin(t3), .3);
	vec3 offset4 = vec3 (.1, -.4*cos(t2), .4*sin(t2));
	vec3 offset5 = vec3 (.4*cos(t1), -.2, .3*sin(t1));
	vec3 offset6 = vec3 (-.2*cos(t3), -.4, -.4*sin(t1));
	vec3 offset7 = vec3 (.3*sin(t2), -.6*cos(t2), .6);
	vec3 offset8 = vec3 (-.3, .5*sin(t3), -.4*cos(t1));

	float ball1 = sdSphere (p + offset1, r4);
	float ball2 = sdSphere (p + offset2, r2);
	float metaBalls = opCombine (ball1, ball2, r1);

	ball1 = sdSphere (p + offset3, r1);
	ball2 = sdSphere (p + offset4, r3);
	metaBalls = opCombine (metaBalls, opCombine (ball1, ball2, .2), r2);

	ball1 = sdSphere (p + offset5, r3);
	ball2 = sdSphere (p + offset6, r2);
	metaBalls = opCombine (metaBalls, opCombine (ball1, ball2, .2), r3);

	ball1 = sdSphere (p + offset7, r3);
	ball2 = sdSphere (p + offset8, r4);
	metaBalls = opCombine (metaBalls, opCombine (ball1, ball2, .2), r4);

	return min (metaBalls, bottom);
}

float raymarch (in vec3 ro, in vec3 rd, inout int iter)
{
	float t = .0;
	float d = .0;
	for (int i = 0; i < MAX_ITER; ++i) {
		vec3 p = ro + d * rd;
		t = scene (p);
		if (abs (t) < EPSILON * (1. + .125*t)) break;
		d += t*STEP_BIAS;
		iter = i;
	}

	return d;
}

vec3 shade (in vec3 ro, in vec3 rd, in float d)
{
	vec3 p = ro + d*rd;
	vec3 ball = p;
	ball.xz *= r2d (-95.*time);
	ball.zy *= r2d (63.*time);

	float floorPhase = cos (45.*p.x);
	float ballPhase = cos (75.*ball.z);
	float floorMask = smoothstep (.01*d, .0025*d, (.5 + .5*floorPhase));
	float ballMask = smoothstep (.01*d, .0025*d, (.5 + .5*ballPhase));
	vec3 floorColor = mix (vec3 (.3, .3, 1.), vec3 (.0), 1. - floorMask);
	vec3 ballColor = mix (vec3 (1., .3, .3), vec3 (.0), 1. - ballMask);
	bool isBall = (p.y > -.75 && p.y < 1.);

	float brightness = 5. / pow (.25*d*d, 1.75);
	ballColor += vec3 (1., .2, .1)*brightness;
	floorColor += vec3 (.1, .2, 1.)*brightness;

	return isBall ? ballColor : floorColor;
}

vec3 camera (in vec2 uv, in vec3 ro, in vec3 aim, in float zoom)
{
	vec3 camForward = normalize (vec3 (aim - ro));
	vec3 worldUp = vec3 (.0, 1., .0);
	vec3 camRight = normalize (cross (worldUp, camForward));
	vec3 camUp = normalize (cross (camForward, camRight));
	vec3 camCenter = ro + camForward * zoom;

	return normalize (camCenter + uv.x*camRight + uv.y*camUp - ro);
}

void main ()
{
	vec2 uv = fragCoord.yx;
	vec2 uvRaw = uv;
	uv = uv*2. - 1.;
	uv.y *= -resolution.x/resolution.y;
	uv *= 1. + .5*length(fragCoord*2. - 1.);

	float angle = radians (300. + 30.*time);
	float dist = 2.5;
	vec3 ro = vec3 (dist*cos (angle), 2., dist*sin (angle));
	vec3 aim = vec3 (.0);
	float zoom = 2. + .5*cos (time);
	vec3 rd = camera (uv, ro, aim, zoom);

	int iter = 0;
	float d = raymarch (ro, rd, iter);
	float fog = 1. / (1. + d*d*.25);
	float glow = float (iter) / float (MAX_ITER);
	vec3 p = ro + d * rd;

	vec3 col = shade (ro, rd, d) + pow (glow, 1.05)*vec3 (1.);

	col *= fog;
	col = col / (1. + col);
	col *= .8 + .2*pow (16.*uvRaw.x*uvRaw.y*(1. - uvRaw.x)*(1. - uvRaw.y), .3);
	col *= mix (1., .75, .5 + .5*cos (2700.*uvRaw.y));
	col = pow (col, vec3 (1./2.2));

	gl_FragColor = vec4 (col*.4, 1.);
}
