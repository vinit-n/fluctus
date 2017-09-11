#include "geom.h"
#include "bvh.cl"
#include "utils.cl"
#include "intersect.cl"
#include "env_map.cl"

kernel void nextVertex(
    global GPUTaskState *tasks,
    global Material *materials,
    global uchar *texData,
    global TexDescriptor *textures,
    global Triangle *tris,
    global GPUNode *nodes,
    global uint *indices,
    global RenderParams *params,
    global RenderStats *stats,
	read_only image2d_t envMap,
    global float *pdfTable,
	global float *pdfTable1D,
    uint numTasks)
{
    const size_t gid = get_global_id(0) + get_global_id(1) * params->width;
    const uint limit = min(params->width * params->height, numTasks); // TODO: remove need for params, use only numTasks!

    if (gid >= limit)
        return;

    // Read the path state
    global PathPhase *phase = (global PathPhase*)&ReadI32(phase, tasks);
    if (*phase != MK_RT_NEXT_VERTEX)
        return;

	// Don't continue paths with near zero pdf => no NaNs (div by zero)
	float pdf = ReadF32(pdf, tasks);
	if (pdf < 1e-6f)
	{
		*phase = MK_SPLAT_SAMPLE;
		return;
	}

	const float3 rayOrig = ReadFloat3(orig, tasks);
    const float3 rayDir = ReadFloat3(dir, tasks);
    Ray r = { rayOrig, rayDir };

    // Trace ray
    Hit hit = EMPTY_HIT(FLT_MAX); // TODO: Max distance?
    bvh_intersect(&r, &hit, tris, nodes, indices);
    if (params->sampleImpl && params->useAreaLight) intersectLight(&hit, &r, params);

    // Write hit to path state
    writeHitSoA(hit, tasks, gid, numTasks);

    // Update render statistics
    global uint *len = &ReadU32(pathLen, tasks);
    atomic_inc((*len == 0) ? &stats->primaryRays : &stats->extensionRays);
    *len += 1;

    // Implicit environment map sample
    if (hit.i < 0)
    {
        float3 bg = (float3)(0.0f, 0.0f, 0.0f);
        if (params->useEnvMap && (*len == 1 || params->sampleImpl))
            bg = evalEnvMapDir(envMap, r.dir) * params->envMapStrength;
        
        // MIS
        float weight = 1.0f;
        bool lastSpecular = ReadU32(lastSpecular, tasks);
        if (params->sampleImpl && params->sampleExpl && params->useEnvMap && *len > 1 && !lastSpecular)
        {
            const float lightPickProb = 1.0f;
            int2 dims = get_image_dim(envMap);
            float directPdfW = envMapPdf(dims.x, dims.y, pdfTable, rayDir);
            float actualPdfW = ReadF32(lastPdfW, tasks);
            weight = (actualPdfW * lightPickProb) / (actualPdfW * lightPickProb + directPdfW);
        }   

        float3 T = ReadFloat3(T, tasks);
		float3 newEi = ReadFloat3(Ei, tasks) + weight * T * bg / pdf;
		WriteFloat3(Ei, tasks, newEi);
		*phase = MK_SPLAT_SAMPLE;
    }
    // Implicit area light sample
    else if (hit.areaLightHit)
    {
		float misWeight = 1.0f;
		bool lastSpecular = ReadU32(lastSpecular, tasks);
		if (params->sampleExpl && *len > 1 && !lastSpecular) // not very direct + MIS needed
		{
			const float directPdfA = 1.0f / (4.0f * params->areaLight.size.x * params->areaLight.size.y);
			const float directPdfW = pdfAtoW(directPdfA, length(hit.P - r.orig), dot(normalize(-r.dir), hit.N));
			const float lightPickProb = 1.0f;
			const float lastPdfW = ReadF32(lastPdfW, tasks);
			misWeight = lastPdfW / (lastPdfW + directPdfW * lightPickProb);
		}

		// Pdf (i.e. extension ray pdf = lastPdfW) included in prob
		float3 T = ReadFloat3(T, tasks);
		float3 newEi = ReadFloat3(Ei, tasks) + T * misWeight * params->areaLight.E / pdf;
		WriteFloat3(Ei, tasks, newEi);
        
		// No reflective lights
        *phase = MK_SPLAT_SAMPLE;
    }
    // Scene hit
	else
	{
		// Read BSDF
		float3 N, Kd, Ks;
		float Ni;
		getMaterialParameters(hit, tris, materials, texData, textures, &Kd, &N, &Ks, &Ni);

		bool backside = dot(hit.N, r.dir) > 0.0f;
		if (backside)
		{
			hit.N *= -1.0f;
		}

		// Refract
		if (Ni > 1.0f)
		{
			float3 orig;
			const float EPS_REFR = 1e-5f;
			float cosI = dot(-normalize(r.dir), hit.N);
			uint seed = ReadU32(seed, tasks);

			float n1 = 1.0f, n2 = Ni;
			if (backside) swap_m(n1, n2, float); // inside of material
			float eta = n1 / n2;

			float cosT = 1.0f - eta * eta * (1.0f - cosI * cosI);
			float raylen = length(r.dir);

			// Total internal reflection
			if (cosT < 0.0f)
			{
				orig = hit.P + EPS_REFR * hit.N;
				r.dir = raylen * reflect(normalize(r.dir), hit.N);
			}
			else
			{
				// Fresnel: reflectance for unpolarized light
				cosT = sqrt(cosT);
				float fr = 0.5f * (pow((n1 * cosI - n2 * cosT) / (n1 * cosI + n2 * cosT), 2.0f) + pow((n2 * cosI - n1 * cosT) / (n1 * cosT + n2 * cosI), 2.0f));

				// Simulate absorption by decreasing throughput
				float3 newT = ReadFloat3(T, tasks) * Ks;

				global float *pdf = &ReadF32(pdf, tasks);
				if (rand(&seed) < fr)
				{
					// Reflection
					orig = hit.P + EPS_REFR * hit.N;
					r.dir = raylen * reflect(normalize(r.dir), hit.N);
					// fr in pdf and T cancel out
				}
				else
				{
					// Refraction
					orig = hit.P - EPS_REFR * hit.N;
					r.dir = raylen * (normalize(r.dir) * eta + hit.N * (eta * cosI - cosT));
					newT *= eta * eta;
					// (1 - fr) in pdf and T cancel out
				}

				WriteFloat3(T, tasks, newT);
			}

			// Update state
			WriteFloat3(orig, tasks, orig);
			WriteFloat3(dir, tasks, r.dir);
			WriteU32(seed, tasks, seed);
			WriteU32(lastSpecular, tasks, 1);
			*phase = MK_RT_NEXT_VERTEX;
		}
		else
		{
			// Explicit light sample (direct lighting)
			WriteU32(lastSpecular, tasks, 0);
			*phase = MK_SAMPLE_LIGHT_EXPL;
		}
    }
}
