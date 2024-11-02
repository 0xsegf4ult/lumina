#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout( push_constant ) uniform ImGUIPushConst
{
        mat4 proj;
};

out gl_PerVertex
{
        vec4 gl_Position;
};

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main()
{
        outUV = inUV;
        outColor = vec4(pow(inColor.rgb, vec3(2.2f)), inColor.a);
        gl_Position = proj * vec4(inPosition, 0.0f, 1.0f);
}                                   
