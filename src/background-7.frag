precision highp float;

uniform vec2 resolution;
uniform float time;

varying vec2 fragCoord;

#define MAX_STEPS 96
#define STEP_BIAS .75
#define EPSILON .001
#define PI 3.14159265359

float hashn (vec3 p, float t)
{
    p = fract (p*.3183099 + .1);
    p *= 17.;
    return max (fract (p.x*p.y*p.z*(p.x + p.y + p.z)), 1. - t);
}

float noise (in vec3 x, in float t)
{
    vec3 p = floor (x);
    vec3 f = fract (x);
    f = f*f*(3. - 2.*f);
	
    return mix(mix(mix( hashn(p+vec3(0,0,0), t), 
                        hashn(p+vec3(1,0,0), t),f.x),
                   mix( hashn(p+vec3(0,1,0), t), 
                        hashn(p+vec3(1,1,0), t),f.x),f.y),
               mix(mix( hashn(p+vec3(0,0,1), t), 
                        hashn(p+vec3(1,0,1), t),f.x),
                   mix( hashn(p+vec3(0,1,1), t), 
                        hashn(p+vec3(1,1,1), t),f.x),f.y),f.z);
}

float smax (in float a, in float b, in float k)
{    
   float f = max(0., 1. - abs(b - a)/k);
   return max(a, b) + k*.25*f*f;
}

float sdSphere (in vec3 p, in float radius)
{
    return length (p) - radius;
}

float sdBox (in vec3 p, in vec3 b)
{
	vec3 d = abs (p) - b;
	return min (max (d.x, max (d.y, d.z)), .0) + length (max (d, .0));
}

mat2 r2d (in float degree)
{
    float rad = radians (degree);
    float c = cos (rad);
    float s = sin (rad);
	return mat2 (c, s, -s, c);
}

vec3 opTwist (in vec3 p)
{
    mat2  m = r2d (p.y*p.x*p.z);
    vec3  q = vec3 (m*p.xz, p.y);
    return q;
}

float scene (in vec3 p, in float t)
{
    vec3 noiseCenter = p;
    noiseCenter.z -= 2.; 
    vec3 boxCenter = noiseCenter;
    vec3 ballCenter = noiseCenter + vec3 (.0, .0, 1. + 1.*(.5 + .5*cos(t))); 

    noiseCenter += .05*opTwist (.5*p);
    noiseCenter.xy *= r2d (10.*t);
    noiseCenter.yz *= r2d (-20.*t);
    noiseCenter.zx *= r2d (30.*t);

    float variation = 6. + 4.*(.5 + .5*cos (14.*t));
    vec3 offset = vec3 (-5.,-1., 1.)*t*.5;
	float f = 3. + 2.5*(.5 + .5*cos (2.5*t));
    float structure = noise (f*noiseCenter + offset, variation) - .65 + (sin (t) + 1.)*.05;

    float box = sdBox (boxCenter, vec3 (6.));
    float ball = sdSphere (ballCenter, 1.5 + .5*(.5 + .5*cos (t)));
    ball = min (ball, sdSphere (ballCenter + vec3 (.0, .0, -1.5), 1.5 + .5*(.5 + .5*cos (t))));
    structure = smax (-ball, structure*.5, .25);

    return smax (box, structure, .25);
}

float distriGGX (in vec3 N, in vec3 H, in float roughness)
{
    float a2     = roughness * roughness;
    float NdotH  = max (dot (N, H), .0);
    float NdotH2 = NdotH * NdotH;

    float nom    = a2;
    float denom  = (NdotH2 * (a2 - 1.) + 1.);
    denom        = PI * denom * denom;

    return nom / denom;
}

float geomSchlickGGX (in float NdotV, in float roughness)
{
    float nom   = NdotV;
    float denom = NdotV * (1. - roughness) + roughness;

    return nom / denom;
}

float geomSmith (in vec3 N, in vec3 V, in vec3 L, in float roughness)
{
    float NdotV = max (dot (N, V), .0);
    float NdotL = max (dot (N, L), .0);
    float ggx1 = geomSchlickGGX (NdotV, roughness);
    float ggx2 = geomSchlickGGX (NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick (in float cosTheta, in vec3 F0, float roughness)
{
	return F0 + (max (F0, vec3(1. - roughness)) - F0) * pow (1. - cosTheta, 5.);
}

vec3 normal (vec3 p, float epsilon, in float t)
{
    float d = scene (p, t);
    vec2 e = vec2 (epsilon, .0);
    return normalize (vec3 (scene(p + e.xyy, t),
                            scene(p + e.yxy, t),
                            scene(p + e.yyx, t)) - d); 
}

vec3 shade (in vec3 ro, in vec3 rd, in float d, in float t)
{
    vec3 p = ro + d * rd;
    vec3 nor = normal (p, d*EPSILON, t);

    // "material" hard-coded for the moment
    vec3 albedo = vec3 (.4);
    float metallic = .2;
    float roughness = .1;

    // lights hard-coded as well atm
    vec3 lightColors[2];
    lightColors[0] = vec3 (.5, .2, .1);
    lightColors[1] = vec3 (.1, .3, .5);;

    vec3 lightPositions[2];
    float c = cos (t);
    float s = sin (t);
    lightPositions[0] = vec3 (-.5*c, -.5*s, -.1);
    lightPositions[1] = vec3 (.5*c, .5*s, 1.);

	vec3 N = normalize (nor);
    vec3 V = normalize (ro - p);

    vec3 F0 = vec3 (.04);
    F0 = mix (F0, albedo, metallic);
    vec3 kD = vec3 (.0);
	           
    // reflectance equation
    vec3 Lo = vec3 (.0);
    for (int i = 0; i < 2; ++i) 
    {
        // calculate per-light radiance
        vec3 L = normalize (lightPositions[i] - p);
        vec3 H = normalize (V + L);
        float dist = distance (p, lightPositions[i]);
        float attenuation = 2./(dist*dist);
        vec3 radiance = lightColors[i]*attenuation;
        
        // cook-torrance brdf
        float aDirect = pow (roughness + 1., 2.);
        float aIBL =  roughness * roughness;
        float NDF = distriGGX (N, H, roughness);
        float G = geomSmith (N, V, L, roughness);
        vec3 F = fresnelSchlick (max (dot (H, V), .0), F0, roughness);

        vec3 kS = F;
        kD = vec3 (1.) - kS;
        kD *= 1. - metallic;
        
        vec3 nominator = NDF * G * F;
        float denominator = 4. * max (dot (N, V), .0) * max (dot (N, L), .0);
        vec3 specular = nominator / max (denominator, .001);  

        // add to outgoing radiance Lo
        float NdotL = max (dot (N, L), .0);                
        Lo += (kD*albedo/PI + specular)*radiance*NdotL;
    }

    vec3 ambient = kD * albedo;

    return ambient + Lo;
}

float trace (in vec3 o, in vec3 r)
{
    float t = .0;
    for (int i = 0; i < MAX_STEPS; ++i) {
       vec3 p = o + r*t;
       float d = scene (p, time);
       t += d*STEP_BIAS;
    }
    return t;
}

void main ()
{
    // normalize and aspect-correct
    vec2 uv = fragCoord.yx;
    uv = uv * 2. - 1.;
    uv.x *= resolution.y/resolution.x;

    // set up view-ray/"camera"
    vec3 r = normalize (vec3 (uv, .75));
    vec3 o = vec3 (0.);

    // determine pixel-color
    float d = trace (o, r);
    vec3 color = shade (o, r, d, time);

    // tone-map, gamma-correct, "fog"
    color = color / (1. + color);
    color = pow (color, vec3 (1./2.2));
    color *= 2.3 - d*.5;

    gl_FragColor = vec4 (color*.55, 1.);
}
