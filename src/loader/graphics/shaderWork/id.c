#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

ShaderFilesToMod idShaderFilesToMod[] = {{"/shader/Cg/inc/es_shd_base.cg", 0, ""},
                                         {"/shader/Cg/inc/es_shd_vsin.cg", 0, ""},
                                         {"/data/Shader/blur_v.fx", 0, ""},
                                         {"/data/Shader/course_v.fx", 0, ""},
                                         {"/data/Shader/edge_v.fx", 0, ""},
                                         {"/data/Shader/glareGenerator_v.fx", 0, ""},
                                         {"/data/Shader/occlusionCuller_v.fx", 0, ""},
                                         {"/data/Shader/stretchFrameBuffer_v.fx", 0, ""},
                                         {"/data/Shader/car_v.fx", 0, ""},
                                         {"/data/Shader/depthConverage_v.fx", 0, ""},
                                         {"/data/Shader/effect_v.fx", 0, ""},
                                         {"/data/Shader/glareSampler_v.fx", 0, ""},
                                         {"/data/Shader/shadowMap_v.fx", 0, ""},
                                         {"/data/Shader/sun_v.fx", 0, ""},
                                         {"/data/Shader/combiner_v.fx", 0, ""},
                                         {"/data/Shader/dof_v.fx", 0, ""},
                                         {"/data/Shader/glareFinalizer_v.fx", 0, ""},
                                         {"/data/Shader/morphImage_v.fx", 0, ""},
                                         {"/data/Shader/shigeno_v.fx", 0, ""},
                                         {"/data/Shader/effect_p.fx", 0, ""}};

int idFilesToModCount = sizeof(idShaderFilesToMod) / sizeof(ShaderFilesToMod);

ShaderFilesToMod id4jShaderFilesToMod[] = {
    {"/shader/Cg/inc/es_shd_base.cg", 0, ""},     {"/shader/Cg/inc/es_shd_vsin.cg", 0, ""}, {"/data/Shader/blur_v.fx", 0, ""},
    {"/data/Shader/course_v.fx", 0, ""},          {"/data/Shader/edge_v.fx", 0, ""},        {"/data/Shader/glareGenerator_v.fx", 0, ""},
    {"/data/Shader/occlusionCuller_v.fx", 0, ""}, {"/data/Shader/car_v.fx", 0, ""},         {"/data/Shader/effect_v.fx", 0, ""},
    {"/data/Shader/glareSampler_v.fx", 0, ""},    {"/data/Shader/shadowMap_v.fx", 0, ""},   {"/data/Shader/sun_v.fx", 0, ""},
    {"/data/Shader/combiner_v.fx", 0, ""},        {"/data/Shader/dof_v.fx", 0, ""},         {"/data/Shader/glareFinalizer_v.fx", 0, ""},
    {"/data/Shader/morphImage_v.fx", 0, ""},      {"/data/Shader/shigeno_v.fx", 0, ""}};

int id4FilesToModCount = sizeof(id4jShaderFilesToMod) / sizeof(ShaderFilesToMod);

SearchReplace idShaderReplaceSet[] = {{"const float4x4  mtxWorldViewProj;", "const float4x4  mtxWorldViewProj : C0;"},
                                      {"const uniform float4x4  mtxWorldViewProj;", "const uniform float4x4  mtxWorldViewProj : C0;"},
                                      {"float4x4    mtxWorldViewProj;", "float4x4    mtxWorldViewProj : C0;"},
                                      {"const uniform float4x4  g_mtxWorldViewProj;", "const uniform float4x4  g_mtxWorldViewProj : C0;"},
                                      {"uniform\tfloat4x4\tmtxWorldViewProj;", "uniform\tfloat4x4\tmtxWorldViewProj : C0;"},
                                      {"const uniform float2 f2UV_Offset", "const uniform float2 f2UV_Offset : C0"},
                                      {"const float4x4 g_m4WorldViewProj;", "const float4x4 g_m4WorldViewProj : C0;"}};

int idShaderReplaceCount = sizeof(idShaderReplaceSet) / sizeof(SearchReplace);

SearchReplace id5ShaderReplaceSetForSmoke[] = {
    {"h4OutputColor.a = h4Color_ColorMap.a;",
     "#if CV_USE_NORMAL_XY_AS_UV && !CV_ENABLE_MATERIAL_COLOR\nh4OutputColor.rgb = half3(0.5h, 0.5h, "
     "0.5h);\nh4OutputColor.a = 0.2h;\n#else\nh4OutputColor.a = h4Color_ColorMap.a;\n#endif"}};

int id5ShaderReplaceSetForSmokeCount = sizeof(id5ShaderReplaceSetForSmoke) / sizeof(SearchReplace);

// Fragment
SearchReplace idFsAti[] = {{"OPTION ARB_fragment_program_shadow;", "#OPTION ARB_fragment_program_shadow;"}, {" SHADOW2D;", " 2D;"}};

int idFsAtiCount = sizeof(idFsAti) / sizeof(SearchReplace);

//   When usin arbvp1 we still need the 192 slots for bones.
SearchReplace idVsBoneMax[] = {{"S_VERTEX_BLEND_BONE_MAX * 3", "192"}};

int idVsBoneMaxCount = sizeof(idVsBoneMax) / sizeof(SearchReplace);

//   Cg builds the relatively addressed constant array as a mix of program.env
// entries and inline literals, which AMD's ARB assembler faults on. Lift the
// literals into PARAMs of their own so the indexed array stays pure program.env.
char *splitParamLiterals(const char *program)
{
    //   Only relatively addressed arrays trip the assembler.
    if (strstr(program, "c[A0.x") == NULL)
        return NULL;

    const char *decl = strstr(program, "PARAM c[");
    if (decl == NULL)
        return NULL;

    const char *env = strstr(decl, "program.env[0..");
    if (env == NULL)
        return NULL;

    const char *declEnd = strstr(env, "};");
    if (declEnd == NULL)
        return NULL;

    int lastEnv = atoi(env + strlen("program.env[0.."));

    const char *cursor = strchr(env, ']');
    if (cursor == NULL || cursor > declEnd)
        return NULL;

    while (cursor < declEnd && (*cursor == ']' || *cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
        cursor++;

    if (*cursor != ',')
        return NULL;

    char decls[1024];
    int declsLen = 0;
    int literals = 0;

    declsLen += sprintf(decls, "PARAM c[%d] = { program.env[0..%d] };\n", lastEnv + 1, lastEnv);

    while (cursor < declEnd)
    {
        const char *open = strchr(cursor, '{');
        if (open == NULL || open > declEnd)
            break;

        const char *close = strchr(open, '}');
        if (close == NULL || close > declEnd)
            break;

        if (declsLen + (int)(close - open) + 32 >= (int)sizeof(decls))
            break;

        declsLen += sprintf(decls + declsLen, "PARAM cLit%d = %.*s};\n", literals, (int)(close - open), open);
        literals++;
        cursor = close + 1;
    }

    if (literals == 0)
        return NULL;

    //   Renumber before prepending: the new declaration spells c[lastEnv + 1],
    // which is also the first literal's old slot.
    char *body = strdup(declEnd + 2);
    if (body == NULL)
        return NULL;

    for (int i = 0; i < literals; i++)
    {
        char search[32];
        char replace[32];
        sprintf(search, "c[%d]", lastEnv + 1 + i);
        sprintf(replace, "cLit%d", i);

        char *next = replaceSubstring(body, 0, strlen(body), search, replace);
        if (next == NULL)
            break;
        free(body);
        body = next;
    }

    size_t headLen = decl - program;
    char *out = malloc(headLen + declsLen + strlen(body) + 1);
    if (out == NULL)
    {
        free(body);
        return NULL;
    }

    memcpy(out, program, headLen);
    memcpy(out + headLen, decls, declsLen);
    strcpy(out + headLen + declsLen, body);
    free(body);

    return out;
}
