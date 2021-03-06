#include "stdafx.h"
#include "SolarSystemGLProgram.h"

#include "SolarSystem.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define GLSL(src) "#version 330\n" #src


SolarSystemGLProgram::SolarSystemGLProgram()
	: nrlights(0), matLocation(0), modelMatLocation(0), transpInvModelMatLocation(0), 
	colorLocation(0), useTextLocation(0), isSunLocation(0), viewPosLocation(0), farPlaneLoc(0),
	lightPosLoc(0), calcShadowsLoc(0), textureLoc(0), depthMapLoc(0)
{
}


SolarSystemGLProgram::~SolarSystemGLProgram()
{
}


void SolarSystemGLProgram::SetupLights(BodyPropList& BodyProperties)
{
//	nrlights = 0;
	lights.clear();


	for (const auto &body : BodyProperties)
		if (body.isSun) {
			lights.emplace_back(Light());
		}

	if (0 == nrlights) lights.emplace_back(Light());

	USES_CONVERSION;

	// lights uniforms positions

	if (nrlights == 0)
	{
		lights[0].attenPos = glGetUniformLocation(getID(), "Lights[0].lightDir");
		lights[0].lightDirPos = glGetUniformLocation(getID(), "Lights[0].atten");
	}
	else
	{
		for (unsigned int light = 0; light < nrlights; ++light)
		{
			CString param;

			param.Format(L"Lights[%d].lightDir", light);
			lights[light].lightDirPos = glGetUniformLocation(getID(), W2A(param));

			param.Format(L"Lights[%d].atten", light);
			lights[light].attenPos = glGetUniformLocation(getID(), W2A(param));
		}
	}
}

void SolarSystemGLProgram::getUniformsLocations()
{
	// vertex shader parameters
	matLocation = glGetUniformLocation(getID(), "transformMat");
	modelMatLocation = glGetUniformLocation(getID(), "modelMat");
	transpInvModelMatLocation = glGetUniformLocation(getID(), "transpInvModelMat");

	// fragment shader uniform parameters
	colorLocation = glGetUniformLocation(getID(), "theColor");
	useTextLocation = glGetUniformLocation(getID(), "UseTexture");
	isSunLocation = glGetUniformLocation(getID(), "IsSun");
	viewPosLocation = glGetUniformLocation(getID(), "viewPos");

	farPlaneLoc = glGetUniformLocation(getID(), "farPlane");
	lightPosLoc = glGetUniformLocation(getID(), "lightPos");
	calcShadowsLoc = glGetUniformLocation(getID(), "calcShadows");

	textureLoc = glGetUniformLocation(getID(), "Texture");
	depthMapLoc = glGetUniformLocation(getID(), "depthMap");
}

bool SolarSystemGLProgram::SetupShaders(unsigned int nrLights)
{
	nrlights = nrLights;
	
	if (!SetupVertexShader()) return false;
	else if (!SetupFragmentShader()) return false;

	Bind();

	if (getStatus() == false)
	{
		AfxMessageBox(CString("Shaders compile: ") + CString(getStatusMessage()));
		theApp.PostThreadMessage(WM_QUIT, 0, 0);
		return false;
	}

	getUniformsLocations();

	return true;
}


bool SolarSystemGLProgram::SetupVertexShader()
{
	OpenGL::VertexShader vertexShader;

	vertexShader.setSource(GLSL(

		layout(location = 0) in vec3 position;
		layout(location = 1) in vec3 normal;
		layout(location = 2) in vec2 texCoord;

		uniform mat4 transformMat;
		uniform mat4 modelMat;
		uniform mat3 transpInvModelMat;

		out vec2 TexCoord;
		out vec3 Normal;
		out vec3 FragPos;

		void main()
		{
			gl_Position = transformMat * modelMat * vec4(position.x, position.y, position.z, 1.0);
			TexCoord = texCoord;
			Normal = normalize(transpInvModelMat * normal);
			FragPos = vec3(modelMat * vec4(position, 1.0f));
		}

	));

	if (vertexShader.getStatus() == false)
	{
		AfxMessageBox(CString("Vertex shader: ") + CString(vertexShader.getStatusMessage()));
		theApp.PostThreadMessage(WM_QUIT, 0, 0);
		return false;
	}

	Attach(vertexShader);

	return true;
}


bool SolarSystemGLProgram::SetupFragmentShader()
{
	OpenGL::FragmentShader fragmentShader;

	CString shaderSrc;
	shaderSrc.Format(CString(GLSL(
		\n#define NRLIGHTS %d\n

		struct Light {
			vec3 lightDir;
			float atten;
		};
		uniform Light Lights[NRLIGHTS];

		uniform vec4 theColor;
		uniform int UseTexture;
		uniform int IsSun;
		uniform vec3 viewPos;
		uniform float farPlane;
		uniform vec3 lightPos;

		in vec2 TexCoord;
		in vec3 Normal;
		in vec3 FragPos;

		out vec4 outputColor;

		uniform sampler2D Texture;

		uniform int calcShadows;
		uniform samplerCube depthMap;

		// Returns a random number - this is found in several places on internet, either in the vec2 or vec3 variant
		// but with different numbers...
		float random(in vec3 seed, in int i)
		{
			return fract(sin(dot(vec4(seed, i), vec4(23.661, 2999.6053, 7.6703, 7229.4241))) * 8747.4231);
		}

		float CalcShadow(in float cosAngle, in float viewDist)
		{
			// Get vector between fragment position and light position
			vec3 fragToLight = FragPos - lightPos;
			// Get current linear depth as the length between the fragment and light position
			float currentDepth = length(fragToLight);

			float shadow = 0.0;
			float closestDepth;

			float bias = clamp(0.05*tan(acos(cosAngle)), 0, 0.05);

			int samples = 16;

			float diskRadius = (1.0 + 150.0 * (viewDist / farPlane)) / 8.0;
			for (int i = 0; i < samples; ++i)
			{
				// most of fragments are either deep in the shadow or in the light
				// bail out if six samples say it's either shadow or light
				// this avoids having 16 samples for each fragment
				// for most of them only 6 samples should be enough
				if (6 == i) {
					if (shadow > 5.9) return 0.5;
					else if (shadow < 0.1) return 0.0;
				}

				// pick three random values for directions in space
				float dir1 = random(FragPos, samples * i);
				float dir2 = random(FragPos, samples * i + 1);
				float dir3 = random(FragPos, samples * i + 2);

				// pick three random signs for them
				dir1 *= (random(FragPos, samples * i + 3) > 0.5 ? -1 : 1);
				dir2 *= (random(FragPos, samples * i + 4) > 0.5 ? -1 : 1);
				dir3 *= (random(FragPos, samples * i + 5) > 0.5 ? -1 : 1);

				// make a random vector out of them
				vec3 radial = vec3(dir1, dir2, dir3);

				// sample from a sphere with radius at most diskRadius
				if (length(radial) != 0) radial = normalize(radial) * diskRadius * random(FragPos, i * samples + 6);

				closestDepth = texture(depthMap, fragToLight + radial).r * farPlane;
				if (currentDepth - bias > closestDepth)	shadow += 1.0;
			}

			return shadow * 0.5 / float(samples);
		}

		vec3 CalcLight(in vec3 lightDir, in vec3 viewDir, in vec3 normal, in vec3 color)
		{
			// Diffuse shading
			float diff = max(dot(normal, lightDir), 0.0);

			// Specular shading
			vec3 halfwayDir = normalize(lightDir + viewDir);
			float spec = pow(max(dot(normal, halfwayDir), 0.0), 32);

			// Combine results								
			return (0.7 * diff + 0.2 * spec) * color;
		}

		void main()
		{
			vec4 color = (UseTexture == 1 ? texture(Texture, TexCoord) : theColor);

			vec3 light;

			if (1 == IsSun)
				light = color.xyz;
			else
			{
				vec3 viewVec = viewPos - FragPos;
				vec3 viewDir = normalize(viewVec);
				vec3 normal = normalize(Normal);

				// ambient
				light = 0.1 * color.xyz;

				// shadow
				float shadow = 0.0;
				if (1 == calcShadows)
				{
					float cosAngle = clamp(dot(normal, viewDir), 0.0, 1.0);
					shadow = CalcShadow(cosAngle, length(viewVec));
				}

				for (int i = 0; i < NRLIGHTS; ++i)
				{
					if (i > 0) shadow = 0;

					light += Lights[i].atten * (1.0 - shadow) * CalcLight(normalize(Lights[i].lightDir), viewDir, normal, color.xyz);
				}
			}

			outputColor = vec4(light, 1);
		}

	)), nrlights == 0 ? 1 : nrlights);

	USES_CONVERSION;
	fragmentShader.setSource(W2A(shaderSrc));


	if (fragmentShader.getStatus() == false)
	{
		AfxMessageBox(CString("Fragment shader: ") + CString(fragmentShader.getStatusMessage()));
		theApp.PostThreadMessage(WM_QUIT, 0, 0);
		return false;
	}

	Attach(fragmentShader);

	return true;
}
