struct PointLight {vec4 position;vec4 color;vec4 history;};
layout(std430,binding=0) buffer LightingPool {uvec4 lightClock;uvec4 lightCounts;PointLight lights[32];};
layout(std430,binding=1) readonly buffer Lamps {uvec4 lampHeader;PointLight lamps[8];};
