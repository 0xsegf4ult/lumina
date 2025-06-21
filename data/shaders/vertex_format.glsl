#ifndef VERTEX_FORMAT_HEADER
#define VERTEX_FORMAT_HEADER

struct VertexPos
{
	float pos_x; 
	float pos_y; 
	float pos_z;
};

struct VertexAttr
{
	float tangentQ;
	float uv_x; 
	float uv_y;
	uint nrmOct;
};

struct SkinnedVertex
{
	vec3 pos;
	float tangentQ;
	vec2 uv;
	uint nrmOct;
	uint joints;
	vec4 weights;
};

#endif
