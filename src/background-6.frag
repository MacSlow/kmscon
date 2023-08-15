precision highp float;

uniform vec2 resolution;
uniform float time;

varying vec2 fragCoord;

float hash1 (float v) { return fract (sin (v)*33543.784583); }
float hash2 (vec2 v) { return hash1 (dot (v,
                                          vec2 (1179.432465,
                                                984.49853))); }

mat2 r2d (float deg)
{
    float rad = radians (deg);
    float c = cos (rad);
    float s = sin (rad);
	return mat2 (c, s, -s, c);
}

vec3 camera (in vec2 uv, in vec3 ro, in vec3 aim, in float zoom)
{
    vec3 camForward = normalize (vec3 (aim - ro));
    vec3 worldUp = vec3 (.0, 1., .0);
    vec3 camRight = normalize (cross (camForward, worldUp));
    vec3 camUp = normalize (cross (camRight, camForward));
    vec3 camCenter = normalize (ro + camForward * zoom);

    return normalize ((camCenter + uv.x*camRight + uv.y*camUp) - ro);
}

void main ()
{
    vec2 uv = fragCoord.yx*2. - 1.;
    uv.x *= resolution.y/resolution.x;

    float w = 2. + 1.*(.5 + .5+cos(.25*time));
    float g = 3. + 2.*cos(.5*time);
	vec3 col = vec3 (.0);
    float seed = time;

    const float numSamples = 64.;
    for (float j = .0; j < numSamples; ++j) {
		float t = time - j/numSamples/60.;
	    vec3 ro = vec3 (.0, .0, 1.);
		vec3 aim = vec3 (.3*cos(t), .2*sin(t), .0);
		float zoom = 2.;
		vec3 rd = camera (uv, ro, aim, zoom);
		rd.xy *= r2d (25.*t);

        vec3 kc = vec3 (1.);
        for (int i = 0; i < 4; ++i) {
            vec3 p;
            vec3 n;
            vec2 luv;
		    float d = .0;
		    float ld = .0;
			int id = 0;

            // walls left and right
            if (rd.x >= .0) {
                d = (w - ro.x)/rd.x;
                p = ro + d*rd;
                n = vec3 (-1., .0, .0);
                luv = p.yz;
            	luv *= 2.;
				id = 1;
            } else {
                d = (-w - ro.x)/rd.x;
                p = ro + d*rd;
                n = vec3 (1., .0, .0);
                luv = p.yz;
            	luv *= 2.;
				id = 2;
            }

            // ground and ceiling
            if (rd.y > .0) {
                ld = (g - ro.y)/rd.y;
                if (ld <= d) {
                    p = ro + ld*rd;
                    n = vec3 (.0, -1., .0);
                    luv = p.xz;
            		luv *= 2.;
					id = 3;
                }
            } else {
                ld = (-g - ro.y)/rd.y;
                if (ld < d) {
                    p = ro + ld*rd;
                    n = vec3 (.0, 1., .0);
                    luv = p.xz;
            		luv *= 2.;
					id = 4;
                }
            }

            // back wall
            if (rd.z < .0) {
                float ldz = (-8. - ro.z)/rd.z;
                if (ldz <= ld && ldz <= d) {
                    p = ro + ldz*rd;
                    n = vec3 (.0, .0, 1.);
                    luv = p.xy;
            		luv *= 1.;
					id = 5;
                }
            }

            luv.y -= 9.*t;
            vec2 cell = floor (luv);
            vec2 cc = (luv - cell)*2. - 1.;
            vec3 me = vec3 (smoothstep (.55, .5, length (cc) - .2));

			if(id != 1 && id != 3 && id != 5)
            	me *= vec3 (.9, .8, .7)*step (.875, hash2 (cell));
			if(id == 1)
            	me *= vec3 (.2, .4, .9)*step (.85, hash2 (cell));
			if(id == 3)
            	me *= vec3 (.4, .8, .1)*step (.825, hash2 (cell));
			if(id == 5) {
            	me = vec3 (.9, .3, .1)*step (.975, .5 + .5*(cos(10.*luv.x+t)));
			}
            
            vec3 ma = vec3 (.35);
            col += kc * me;
            kc *= ma;
            float mr = .025 + .5*(hash2 (cell)*2. - 1.);

            ro = p;
            rd = normalize (mix (reflect (rd, n),
                                 vec3 (hash1 (seed += p.x),
                                       hash1 (seed += p.y),
                                       hash1 (seed += p.z))*2. - 1.,
                                 mr));
        }
    }
	col /= numSamples;

    col = col / (1. + col);
    col = pow (col, vec3 (1./2.2));
    gl_FragColor = vec4 (col*.55, 1.);
}

