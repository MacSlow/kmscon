/*
 * kmscon - Vertex Shader
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
 * Vertex Shader
 * This shader is a very basic vertex shader which forwards all data and
 * performs basic matrix multiplications.
 */

uniform mat4 projection;
uniform float orientation;

attribute vec2 position;
attribute vec2 texture_position;
attribute vec3 fgcolor;
attribute vec3 bgcolor;

varying vec2 texpos;
varying vec3 fgcol;
varying vec3 bgcol;

vec2 opRotate(in vec2 p, in float degrees)
{
    float rad = radians(degrees);
    float c = cos(rad);
    float s = sin(rad);
    return p * mat2(vec2(c, s), vec2(-s, c));
}

void main()
{
	vec2 rotatedPosition = opRotate(position, orientation);
	gl_Position = projection * vec4(rotatedPosition, 0.0, 1.0);
	texpos = texture_position;
	fgcol = fgcolor;
	bgcol = bgcolor;
}
