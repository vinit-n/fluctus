#pragma once

#ifdef GPU
typedef float cl_float;
typedef int cl_int;
typedef unsigned int cl_uint;
typedef char cl_uchar;
typedef bool cl_bool;
typedef float2 vfloat2;
typedef float3 vfloat3;
#else
#include "cl2.hpp"
#include "math/float2.hpp"
#include "math/float3.hpp"
typedef FireRays::float3 vfloat3;
typedef FireRays::float2 vfloat2;
#endif

#define PI (3.14159265358979323846f)
#define M_INV_PI (0.3183098861837907f)
#define M_2PI_F (6.2831853071795864f)
#define toRad(deg) (deg * PI / 180)

// For handling SoA data, only used on GPU
// Variable names gid, numTasks are assumed for brevity
#ifndef USE_SOA
#define ReadF32(member, ptr) ptr[gid].member
#define WriteF32(member, ptr, value) ptr[gid].member = value
#define ReadI32(member, ptr) ptr[gid].member
#define WriteI32(member, ptr, value) ptr[gid].member = value
#define ReadU32(member, ptr) ptr[gid].member
#define WriteU32(member, ptr, value) ptr[gid].member = value
#define ReadFloat2(member, ptr) ptr[gid].member
#define ReadFloat3(member, ptr) ptr[gid].member
#define WriteFloat2(member, ptr, value) ptr[gid].member = (vfloat2)(value.x, value.y)
#define WriteFloat3(member, ptr, value) ptr[gid].member = (vfloat3)(value.x, value.y, value.z)
#else
#define OffsetOf(member) (uint)(&((GPUTaskState*)0)->member)
#define ReadF32(member, ptr) ((global float*)ptr)[OffsetOf(member) / (uint)sizeof(float) * numTasks + gid]
#define WriteF32(member, ptr, value) ReadF32(member, ptr) = value
#define ReadF32Vec(member, cmp, ptr) ((global float*)ptr)[(OffsetOf(member) + cmp * (uint)sizeof(float)) / (uint)sizeof(float) * numTasks + gid]
#define ReadI32(member, ptr) ((global int*)ptr)[OffsetOf(member) / (uint)sizeof(int) * numTasks + gid]
#define WriteI32(member, ptr, value) ReadI32(member, ptr) = value
#define ReadU32(member, ptr) ((global uint*)ptr)[OffsetOf(member) / (uint)sizeof(uint) * numTasks + gid]
#define WriteU32(member, ptr, value) ReadU32(member, ptr) = value
#define ReadFloat2(member, ptr) (vfloat2)(ReadF32Vec(member, 0, ptr), ReadF32Vec(member, 1, ptr))
#define ReadFloat3(member, ptr) (vfloat3)(ReadF32Vec(member, 0, ptr), ReadF32Vec(member, 1, ptr), ReadF32Vec(member, 2, ptr))
#define WriteFloat2(member, ptr, value) ReadF32Vec(member, 0, ptr) = value.x; ReadF32Vec(member, 1, ptr) = value.y;
#define WriteFloat3(member, ptr, value) ReadF32Vec(member, 0, ptr) = value.x; ReadF32Vec(member, 1, ptr) = value.y; ReadF32Vec(member, 2, ptr) = value.z;
#endif

typedef struct
{
    vfloat3 orig;
    vfloat3 dir;
} Ray;

typedef struct
{
    vfloat3 P;    // 16B
    vfloat3 Kd;   // 16B
    cl_float R;  // 4B (padded to 16B?)
} Sphere;        // 48B

typedef struct
{
    vfloat3 min;
    vfloat3 max;
} AABB;

typedef struct
{
    AABB box;
    cl_int parent;
    union {
        cl_uint iStart;     // leaf node, index into index list
        cl_uint rightChild; // internal node, index into node vector (left child always current + 1)
    };
    cl_uchar nPrims;        // 0 for interior nodes
} GPUNode;

typedef struct
{
    vfloat3 p; // 16B
    vfloat3 n; // 16B
    vfloat3 t; // 16B
} Vertex; // >= 48B

typedef struct
{
    Vertex v0;
    Vertex v1;
    Vertex v2;
    cl_int matId;
} Triangle; // this struct is used interchangeably with RTTriangle...sizes must match!

typedef struct
{
    vfloat3 E;   // Diffuse emission (W/m^2), ~'color * intensity'?
    vfloat3 pos;
} PointLight;

typedef struct
{
    vfloat3 right;
    vfloat3 up;
    vfloat3 N;
    vfloat3 pos;
    vfloat3 E;        // Diffuse emission (W/m^2)
    vfloat2 size;     // Half of the total width/height, measured from center
} AreaLight;

typedef struct
{
    vfloat3 Kd;     // diffuse reflectivity
    vfloat3 Ks;     // specular reflectivity 
    vfloat3 Ke;     // emission
    cl_float Ns;   // specular exponent (shininess), normally in [0, 1000]
    cl_float Ni;   // index of refraction
    cl_int map_Kd; // diffuse texture descriptor idx
    cl_int map_Ks; // specular texture descriptor idx
    cl_int map_N;  // normal texture descriptor idx
    cl_int type;   // BXDF type, defined in bxdf.cl
} Material;

typedef struct
{
    cl_uint offset; // start of texture data in global array
    cl_uint width;
    cl_uint height;
} TexDescriptor;

typedef struct
{
    vfloat3 P;
    vfloat3 N;
    vfloat2 uvTex;
    cl_float t;
    cl_int i; // index of hit triangle, -1 by default
    cl_int areaLightHit;
    cl_int matId; // index of hit material
} Hit;

#define EMPTY_HIT(tmax) { (vfloat3)(0.0f), (vfloat3)(0.0f), (vfloat2)(0.0f), tmax, -1, 0, -1 }

typedef struct
{
    vfloat3 pos;     // 16B
    vfloat3 dir;     // 16B
    vfloat3 up;      // 16B
    vfloat3 right;   // 16B
    cl_float fov;   // 4B
    cl_float apertureSize; // DoF
    cl_float focalDist;    // DoF
} Camera;

typedef struct
{
    cl_float exposure;
    cl_uint tmOperator;
} PostProcessParams;

typedef struct
{
    AreaLight areaLight;
    Camera camera;
    PostProcessParams ppParams;
    cl_uint width;         // window width
    cl_uint height;        // window height
    cl_uint n_tris;
    cl_uint useEnvMap;
    cl_uint useAreaLight;
    cl_float envMapStrength;
    cl_uint maxBounces;
    cl_uint sampleImpl;    // use implicit light source sampling
    cl_uint sampleExpl;    // use next event estimation
    cl_uint useRoulette;   // Luminance-based russian roulette
    cl_uint wfSeparateQueues;
    cl_float worldRadius;
} RenderParams;


// Microkernel state structs
typedef enum
{
    MK_RT_NEXT_VERTEX = 0,
    MK_SAMPLE_BSDF = 1,
    MK_SAMPLE_LIGHT_IMPL = 2,
    MK_HIT_NOTHING = 3,
    MK_SPLAT_SAMPLE = 4,
    MK_GENERATE_CAMERA_RAY = 5,
    MK_DONE = 6
} PathPhase;


// State for a single path in the microkernel paradigm.
// Stored in SoA format, hence no structs (Laine 2013: 'Megakernels Considered Harmful')
// Laine: 212 bytes per path
typedef struct
{
    // Path state:
    vfloat3 orig;     // path segment origin
    vfloat3 dir;      // path segment direction
    vfloat3 shadowOrig;
    vfloat3 shadowDir;
    vfloat3 T;        // throughput * pdf (for numerical stability)
    vfloat3 Ei;       // irradiance
    vfloat3 lastBsdf; // added to Ei if shadow ray unblocked
    vfloat3 lastEmission;
    vfloat3 lastT;
    // Last hit:
    vfloat3 P;
    vfloat3 N;
    vfloat2 uvTex;
    // Path state:
    PathPhase phase;
	cl_float lastPdfW; // prev. brdf pdf, for MIS (implicit light samples)
    cl_uint pathLen; // number of segments in path
    cl_uint seed;
	cl_uint lastSpecular; // prevents NEE
    cl_uint shadowRayBlocked;
    cl_uint backfaceHit; // for certain bsdf functions
    cl_uint pixelIndex;
    cl_uint firstDiffuseHit; // for accumulating denoiser optional features
    // Previously evaluated light sample
    cl_float lastPdfDirect;    // pdfW of sampled NEE sample
    cl_float lastPdfImplicit;  // pdfW of implicit NEE sample
    cl_float lastCosTh;
    cl_float lastLightPickProb;
    cl_float shadowRayLen;
    // Last hit:
    cl_float t;
    cl_int i;        // index of hit triangle, -1 by default
    cl_int areaLightHit;
    cl_int matId;    // index of hit material
} GPUTaskState;

// Atomic counters for queues
// Incremented once per workgroup for efficiency
typedef struct
{
    // Path state queues
    cl_uint raygenQueue;
    cl_uint extensionQueue;
    cl_uint shadowQueue;
    // Material queues
    cl_uint diffuseQueue;
    cl_uint glossyQueue;
    cl_uint ggxReflQueue;
    cl_uint ggxRefrQueue;
    cl_uint deltaQueue;
} QueueCounters;

typedef struct
{
    cl_uint primaryRays;
    cl_uint extensionRays;
    cl_uint shadowRays;
    cl_uint samples;
} RenderStats;
