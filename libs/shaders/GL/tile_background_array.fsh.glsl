layout (binding = 1) uniform MEDIUM_P sampler2DArray u_colorTex;

layout (location = 0) in vec4 v_texCoords;

layout (location = 0) out vec4 v_FragColor;

void main()
{
  v_FragColor = texture(u_colorTex, v_texCoords.xyz);
  v_FragColor.a *= v_texCoords.w;
}
