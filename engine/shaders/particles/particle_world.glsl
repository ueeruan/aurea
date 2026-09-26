// Particle World mobile model. Stateless evaluation makes scrubbing, reverse
// playback and export independent of the order in which frames are requested.
// Native implementation; the proprietary CC random sequence is not reproduced.
bool world_particle() { return p.emitShape.x >= 9.5 && p.emitShape.x < 13.5; }

vec3 world_direction(inout uint seed) {
    float z = rnd(seed) * 2.0 - 1.0;
    float azimuth = rnd(seed) * (2.0 * kPi);
    float radial = sqrt(max(0.0, 1.0 - z*z));
    return vec3(radial*cos(azimuth), radial*sin(azimuth), z);
}

vec3 world_velocity(float speed, float direction, float spread, inout uint seed) {
    vec3 unit = world_direction(seed);
    if (p.emitShape.x > 10.5 && p.emitShape.x < 11.5) {
        // Uniform solid-angle cone; no overpopulation of the axis or poles.
        float axial = mix(cos(clamp(spread, 0.0, kPi) * .5), 1.0, rnd(seed));
        float angle = rnd(seed) * 2.0 * kPi;
        float radial = sqrt(max(0.0, 1.0 - axial*axial));
        vec3 axis = vec3(cos(direction), sin(direction), 0);
        vec3 side = vec3(-sin(direction), cos(direction), 0);
        unit = axis*axial + side*(radial*cos(angle)) + vec3(0,0,radial*sin(angle));
    }
    return unit * speed;
}

vec3 world_path(vec3 start, vec3 origin, vec3 velocity, vec3 acceleration,
                float age, out vec3 currentVelocity, out vec3 gravityShift) {
    float drag = max(p.physics.x, 0.0);
    float x = drag * age;
    // Stable small-x limits, including exactly zero drag.
    float decay = exp(-x);
    float travel = x < .001 ? age*(1.0-x*.5+x*x/6.0) : (1.0-decay)/drag;
    float fall = x < .001 ? age*age*(.5-x/6.0+x*x/24.0) : (age-travel)/drag;
    gravityShift = acceleration * fall;
    vec3 displacement = start-origin + velocity*travel;
    currentVelocity = velocity*decay + acceleration*travel;
    if (p.emitShape.x > 11.5 && p.emitShape.x < 12.5) {
        float angle = age * 3.0;
        mat2 spin = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
        displacement.xz = spin * displacement.xz;
        currentVelocity.xz = spin * (velocity.xz*decay) + 3.0*vec2(-displacement.z, displacement.x);
    }
    return origin + displacement + gravityShift;
}
