#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D hdrbuffer;
layout(binding = 1) uniform sampler2D bloombuffer;

layout(push_constant) uniform pcb
{
	float exposure;
	float bloom_weight;
};

vec3 applyACES(vec3 color)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;

	return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
	vec3 hdr_color = texture(hdrbuffer, inUV).xyz;
	vec3 bloom_color = texture(bloombuffer, inUV).xyz;

	vec3 color = mix(hdr_color, bloom_color, bloom_weight);

	vec3 mapped = applyACES(color * exposure);

	outColor = vec4(mapped, 1.0f);

}
