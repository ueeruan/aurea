#ifndef AUREA_COLOR_OKLAB
#define AUREA_COLOR_OKLAB
// Oklab (Björn Ottosson, domínio público) sobre RGB LINEAR.
vec3 aurea_linear_to_oklab(vec3 c) {
    vec3 lms=vec3(dot(c,vec3(.4122214708,.5363325363,.0514459929)),
                  dot(c,vec3(.2119034982,.6806995451,.1073969566)),
                  dot(c,vec3(.0883024619,.2817188376,.6299787005)));
    lms=sign(lms)*pow(abs(lms),vec3(1.0/3.0));
    return vec3(dot(lms,vec3(.2104542553,.7936177850,-.0040720468)),
                dot(lms,vec3(1.9779984951,-2.4285922050,.4505937099)),
                dot(lms,vec3(.0259040371,.7827717662,-.8086757660)));
}
vec3 aurea_oklab_to_linear(vec3 o) {
    vec3 lms=vec3(o.x+.3963377774*o.y+.2158037573*o.z,
                  o.x-.1055613458*o.y-.0638541728*o.z,
                  o.x-.0894841775*o.y-1.2914855480*o.z);
    lms=lms*lms*lms;
    return vec3(dot(lms,vec3(4.0767416621,-3.3077115913,.2309699292)),
                dot(lms,vec3(-1.2684380046,2.6097574011,-.3413193965)),
                dot(lms,vec3(-.0041960863,-.7034186147,1.7076147010)));
}
#endif
