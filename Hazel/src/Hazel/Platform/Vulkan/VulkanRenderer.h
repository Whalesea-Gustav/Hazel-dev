#pragma once

#include "Hazel/Renderer/RendererAPI.h"

namespace Hazel {

	class VulkanRenderer : public RendererAPI
	{
	public:
		virtual void Init() override;
		virtual void Shutdown() override;

		virtual RendererCapabilities& GetCapabilities() override;

		virtual void BeginFrame() override;
		virtual void EndFrame() override;

		virtual void BeginRenderPass(const Ref<RenderPass>& renderPass) override;
		virtual void EndRenderPass() override;
		virtual void SubmitFullscreenQuad(Ref<Pipeline> pipeline, Ref<Material> material) override;

		virtual void SetSceneEnvironment(Ref<Environment> environment, Ref<Image2D> shadow) override;
		virtual std::pair<Ref<TextureCube>, Ref<TextureCube>> CreateEnvironmentMap(const std::string& filepath) override;
		virtual Ref<TextureCube> CreatePreethamSky(float turbidity, float azimuth, float inclination) override;

		virtual void RenderMesh(Ref<Pipeline> pipeline, Ref<Mesh> mesh, const glm::mat4& transform) override;
		virtual void RenderMeshWithoutMaterial(Ref<Pipeline> pipeline, Ref<Mesh> mesh, const glm::mat4& transform) override;
		virtual void RenderQuad(Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform) override;
		
	};

}