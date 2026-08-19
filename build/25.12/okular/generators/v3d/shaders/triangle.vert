#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout (location = 0) out vec3 Normal;
layout (location = 1) out vec3 FragPos;

layout(push_constant) uniform PushConsts {
	mat4 mvp;
} pushConsts;

void main() {
	mat4 model = mat4(1.0);
	FragPos = vec3(model * vec4(inPos.xyz, 1.0));

	//Normal = vec3(pushConsts.mvp * vec4(inNormal, 1.0));
	Normal = inNormal * 0.5 + 0.5;
	gl_Position = pushConsts.mvp * vec4(inPos.xyz, 1.0);
}
