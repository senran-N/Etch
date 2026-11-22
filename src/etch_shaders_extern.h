// Private extern declarations for compiled GPU shader byte arrays (used when ETCH_UNITY_BUILD=OFF).
// Sizes match the checked-in src/shaders_gen/*.h headers; when ETCH_COMPILE_SHADERS=ON and DXC runs,
// CMake generates an overriding copy of this header inside the build directory's shaders_gen folder.
#ifndef ETCH_SHADERS_EXTERN_H
#define ETCH_SHADERS_EXTERN_H

extern const unsigned char g_etch_uber_vs_dxil[13576];
extern const unsigned char g_etch_uber_vs_spv[30884];
extern const unsigned char g_etch_uber_vs_msl[48077];
extern const unsigned char g_etch_uber_ps_dxil[37744];
extern const unsigned char g_etch_uber_ps_spv[105200];
extern const unsigned char g_etch_uber_ps_msl[226176];
extern const unsigned char g_etch_quad_vs_dxil[9936];
extern const unsigned char g_etch_quad_vs_spv[16408];
extern const unsigned char g_etch_quad_vs_msl[17216];
extern const unsigned char g_etch_quad_ps_dxil[12172];
extern const unsigned char g_etch_quad_ps_spv[26072];
extern const unsigned char g_etch_quad_ps_msl[31984];
extern const unsigned char g_etch_subpixel_vs_dxil[7464];
extern const unsigned char g_etch_subpixel_vs_spv[6508];
extern const unsigned char g_etch_subpixel_vs_msl[5703];
extern const unsigned char g_etch_subpixel_ps_dxil[8576];
extern const unsigned char g_etch_subpixel_ps_spv[13676];
extern const unsigned char g_etch_subpixel_ps_msl[15414];
extern const unsigned char g_etch_kawase_vs_dxil[3024];
extern const unsigned char g_etch_kawase_vs_spv[728];
extern const unsigned char g_etch_kawase_vs_msl[510];
extern const unsigned char g_etch_kawase_ps_dxil[6252];
extern const unsigned char g_etch_kawase_ps_spv[5456];
extern const unsigned char g_etch_kawase_ps_msl[4838];

#endif // ETCH_SHADERS_EXTERN_H
