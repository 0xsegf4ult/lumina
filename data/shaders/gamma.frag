#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D hdrbuffer;

layout(push_constant) uniform pcb
{
	float gamma;
	float exposure;
};

void main()
{
	vec3 hdr_color = texture(hdrbuffer, inUV).xyz;
	vec3 mapped = vec3(1.0f) - exp(-hdr_color * exposure);
	mapped = pow(mapped, vec3(1.0f / gamma));

	outColor = vec4(mapped, 1.0f);
}
