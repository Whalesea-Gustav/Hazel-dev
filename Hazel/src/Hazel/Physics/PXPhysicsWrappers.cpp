#include "hzpch.h"
#include "PXPhysicsWrappers.h"
#include "Physics.h"
#include "PhysicsLayer.h"
#include "PhysicsActor.h"
#include "Hazel/Script/ScriptEngine.h"

#include <glm/gtx/rotate_vector.hpp>

#include <PhysX/extensions/PxRigidActorExt.h>

#include "Hazel/Math/Math.h"

namespace Hazel {

	static PhysicsErrorCallback s_ErrorCallback;
	static PhysicsAssertHandler s_AssertHandler;
	static physx::PxDefaultAllocator s_Allocator;
	static physx::PxFoundation* s_Foundation = nullptr;
	static physx::PxPvd* s_PVD = nullptr;
	static physx::PxPhysics* s_Physics = nullptr;
	static physx::PxCooking* s_CookingFactory = nullptr;
	static physx::PxOverlapHit s_OverlapBuffer[OVERLAP_MAX_COLLIDERS];
	static physx::PxDefaultCpuDispatcher* s_CPUDispatcher = nullptr;

	static ContactListener3D s_ContactListener;

	void PhysicsErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
	{
		const char* errorMessage = NULL;

		switch (code)
		{
		case physx::PxErrorCode::eNO_ERROR:				errorMessage = "No Error"; break;
		case physx::PxErrorCode::eDEBUG_INFO:			errorMessage = "Info"; break;
		case physx::PxErrorCode::eDEBUG_WARNING:		errorMessage = "Warning"; break;
		case physx::PxErrorCode::eINVALID_PARAMETER:	errorMessage = "Invalid Parameter"; break;
		case physx::PxErrorCode::eINVALID_OPERATION:	errorMessage = "Invalid Operation"; break;
		case physx::PxErrorCode::eOUT_OF_MEMORY:		errorMessage = "Out Of Memory"; break;
		case physx::PxErrorCode::eINTERNAL_ERROR:		errorMessage = "Internal Error"; break;
		case physx::PxErrorCode::eABORT:				errorMessage = "Abort"; break;
		case physx::PxErrorCode::ePERF_WARNING:			errorMessage = "Performance Warning"; break;
		case physx::PxErrorCode::eMASK_ALL:				errorMessage = "Unknown Error"; break;
		}

		switch (code)
		{
		case physx::PxErrorCode::eNO_ERROR:
		case physx::PxErrorCode::eDEBUG_INFO:
			HZ_CORE_INFO("[PhysX]: {0}: {1} at {2} ({3})", errorMessage, message, file, line);
			break;
		case physx::PxErrorCode::eDEBUG_WARNING:
		case physx::PxErrorCode::ePERF_WARNING:
			HZ_CORE_WARN("[PhysX]: {0}: {1} at {2} ({3})", errorMessage, message, file, line);
			break;
		case physx::PxErrorCode::eINVALID_PARAMETER:
		case physx::PxErrorCode::eINVALID_OPERATION:
		case physx::PxErrorCode::eOUT_OF_MEMORY:
		case physx::PxErrorCode::eINTERNAL_ERROR:
			HZ_CORE_ERROR("[PhysX]: {0}: {1} at {2} ({3})", errorMessage, message, file, line);
			break;
		case physx::PxErrorCode::eABORT:
		case physx::PxErrorCode::eMASK_ALL:
			HZ_CORE_FATAL("[PhysX]: {0}: {1} at {2} ({3})", errorMessage, message, file, line);
			HZ_CORE_ASSERT(false);
			break;
		}
	}

	void ContactListener3D::onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count)
	{
		PX_UNUSED(constraints);
		PX_UNUSED(count);
	}

	void ContactListener3D::onWake(physx::PxActor** actors, physx::PxU32 count)
	{
		for (uint32_t i = 0; i < count; i++)
		{
			physx::PxActor& actor = *actors[i];
			Entity& entity = *(Entity*)actor.userData;

			HZ_CORE_INFO("PhysX Actor waking up: ID: {0}, Name: {1}", entity.GetUUID(), entity.GetComponent<TagComponent>().Tag);
		}
	}

	void ContactListener3D::onSleep(physx::PxActor** actors, physx::PxU32 count)
	{
		for (uint32_t i = 0; i < count; i++)
		{
			physx::PxActor& actor = *actors[i];
			Entity& entity = *(Entity*)actor.userData;

			HZ_CORE_INFO("PhysX Actor going to sleep: ID: {0}, Name: {1}", entity.GetUUID(), entity.GetComponent<TagComponent>().Tag);
		}
	}

	void ContactListener3D::onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, physx::PxU32 nbPairs)
	{
		Entity& a = *(Entity*)pairHeader.actors[0]->userData;
		Entity& b = *(Entity*)pairHeader.actors[1]->userData;

		if (pairs->flags == physx::PxContactPairFlag::eACTOR_PAIR_HAS_FIRST_TOUCH)
		{
			if (ScriptEngine::IsEntityModuleValid(a)) ScriptEngine::OnCollisionBegin(a);
			if (ScriptEngine::IsEntityModuleValid(b)) ScriptEngine::OnCollisionBegin(b);
		}
		else if (pairs->flags == physx::PxContactPairFlag::eACTOR_PAIR_LOST_TOUCH)
		{
			if (ScriptEngine::IsEntityModuleValid(a)) ScriptEngine::OnCollisionEnd(a);
			if (ScriptEngine::IsEntityModuleValid(b)) ScriptEngine::OnCollisionEnd(b);
		}
	}

	void ContactListener3D::onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count)
	{
		Entity& a = *(Entity*)pairs->triggerActor->userData;
		Entity& b = *(Entity*)pairs->otherActor->userData;

		if (pairs->status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
		{
			if (ScriptEngine::IsEntityModuleValid(a)) ScriptEngine::OnTriggerBegin(a);
			if (ScriptEngine::IsEntityModuleValid(b)) ScriptEngine::OnTriggerBegin(b);
		}
		else if (pairs->status == physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
		{
			if (ScriptEngine::IsEntityModuleValid(a)) ScriptEngine::OnTriggerEnd(a);
			if (ScriptEngine::IsEntityModuleValid(b)) ScriptEngine::OnTriggerEnd(b);
		}
	}

	void ContactListener3D::onAdvance(const physx::PxRigidBody* const* bodyBuffer, const physx::PxTransform* poseBuffer, const physx::PxU32 count)
	{
		PX_UNUSED(bodyBuffer);
		PX_UNUSED(poseBuffer);
		PX_UNUSED(count);
	}

	static physx::PxBroadPhaseType::Enum HazelToPhysXBroadphaseType(BroadphaseType type)
	{
		switch (type)
		{
		case Hazel::BroadphaseType::SweepAndPrune: return physx::PxBroadPhaseType::eSAP;
		case Hazel::BroadphaseType::MultiBoxPrune: return physx::PxBroadPhaseType::eMBP;
		case Hazel::BroadphaseType::AutomaticBoxPrune: return physx::PxBroadPhaseType::eABP;
		}

		return physx::PxBroadPhaseType::eABP;
	}

	static physx::PxFrictionType::Enum HazelToPhysXFrictionType(FrictionType type)
	{
		switch (type)
		{
		case Hazel::FrictionType::Patch:			return physx::PxFrictionType::ePATCH;
		case Hazel::FrictionType::OneDirectional:	return physx::PxFrictionType::eONE_DIRECTIONAL;
		case Hazel::FrictionType::TwoDirectional:	return physx::PxFrictionType::eTWO_DIRECTIONAL;
		}

		return physx::PxFrictionType::ePATCH;
	}

	physx::PxScene* PXPhysicsWrappers::CreateScene()
	{
		if (!s_CPUDispatcher)
			s_CPUDispatcher = physx::PxDefaultCpuDispatcherCreate(1);

		physx::PxSceneDesc sceneDesc(s_Physics->getTolerancesScale());

		const PhysicsSettings& settings = Physics::GetSettings();

		sceneDesc.gravity = ToPhysXVector(settings.Gravity);
		sceneDesc.broadPhaseType = HazelToPhysXBroadphaseType(settings.BroadphaseAlgorithm);
		sceneDesc.cpuDispatcher = s_CPUDispatcher;
		sceneDesc.filterShader = HazelFilterShader;
		sceneDesc.simulationEventCallback = &s_ContactListener;
		sceneDesc.frictionType = HazelToPhysXFrictionType(settings.FrictionModel);

		HZ_CORE_ASSERT(sceneDesc.isValid());
		return s_Physics->createScene(sceneDesc);
	}

	void PXPhysicsWrappers::AddBoxCollider(PhysicsActor& actor)
	{
		auto& collider = actor.m_Entity.GetComponent<BoxColliderComponent>();

		if (!collider.Material)
			collider.Material = Ref<PhysicsMaterial>::Create(0.6f, 0.6f, 0.0f);

		glm::vec3 colliderSize = actor.m_Entity.Transform().Scale * collider.Size;
		physx::PxBoxGeometry boxGeometry = physx::PxBoxGeometry(colliderSize.x / 2.0f, colliderSize.y / 2.0f, colliderSize.z / 2.0f);
		physx::PxMaterial* material = s_Physics->createMaterial(collider.Material->StaticFriction, collider.Material->DynamicFriction, collider.Material->Bounciness);
		physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor.m_ActorInternal, boxGeometry, *material);
		shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !collider.IsTrigger);
		shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, collider.IsTrigger);
		shape->setLocalPose(ToPhysXTransform(glm::translate(glm::mat4(1.0f), collider.Offset)));

		material->release();
	}

	void PXPhysicsWrappers::AddSphereCollider(PhysicsActor& actor)
	{
		auto& collider = actor.m_Entity.GetComponent<SphereColliderComponent>();

		if (!collider.Material)
			collider.Material = Ref<PhysicsMaterial>::Create(0.6f, 0.6f, 0.0f);

		glm::vec3 actorScale = actor.m_Entity.Transform().Scale;
		// We effectively do the same thing as Unitys SphereColliders here, where the radius is multiplied with the biggest scale value
		float largestComponent = glm::max(actorScale.x, glm::max(actorScale.y, actorScale.z));

		physx::PxSphereGeometry sphereGeometry = physx::PxSphereGeometry(largestComponent * collider.Radius);
		physx::PxMaterial* material = s_Physics->createMaterial(collider.Material->StaticFriction, collider.Material->DynamicFriction, collider.Material->Bounciness);
		physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor.m_ActorInternal, sphereGeometry, *material);
		shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !collider.IsTrigger);
		shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, collider.IsTrigger);

		material->release();
	}

	void PXPhysicsWrappers::AddCapsuleCollider(PhysicsActor& actor)
	{
		auto& collider = actor.m_Entity.GetComponent<CapsuleColliderComponent>();

		if (!collider.Material)
			collider.Material = Ref<PhysicsMaterial>::Create(0.6f, 0.6f, 0.0f);

		glm::vec3 size = actor.m_Entity.Transform().Scale;
		// NOTE(Peter): CapsuleGeometry expects half height
		float radiusScale = glm::max(size.x, size.z);
		physx::PxCapsuleGeometry capsuleGeometry = physx::PxCapsuleGeometry(radiusScale * collider.Radius, size.y * (collider.Height / 2.0f));
		physx::PxMaterial* material = s_Physics->createMaterial(collider.Material->StaticFriction, collider.Material->DynamicFriction, collider.Material->Bounciness);
		physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*actor.m_ActorInternal, capsuleGeometry, *material);
		shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !collider.IsTrigger);
		shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, collider.IsTrigger);
		shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 0, 1))));

		material->release();
	}

	void PXPhysicsWrappers::AddMeshCollider(PhysicsActor& actor)
	{
		auto& collider = actor.m_Entity.GetComponent<MeshColliderComponent>();

		if (!collider.Material)
			collider.Material = Ref<PhysicsMaterial>::Create(0.6f, 0.6f, 0.0f);

		glm::vec3 size = actor.m_Entity.Transform().Scale;
		physx::PxMaterial* material = s_Physics->createMaterial(collider.Material->StaticFriction, collider.Material->DynamicFriction, collider.Material->Bounciness);
		physx::PxMaterial* materials[] = { material };

		if (collider.IsConvex)
		{
			// Remove any potential triangle meshes from this actor
			std::vector<physx::PxShape*> shapes = CreateConvexMesh(collider, size);

			for (auto shape : shapes)
			{
				shape->setMaterials(materials, 1);
				shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !collider.IsTrigger);
				shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, collider.IsTrigger);
				actor.AddCollisionShape(shape);
			}
		}
		else
		{
			// Remove any potential convex meshes from this actor
			std::vector<physx::PxShape*> shapes = CreateTriangleMesh(collider, size);

			for (auto shape : shapes)
			{
				shape->setMaterials(materials, 1);
				shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !collider.IsTrigger);
				shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, collider.IsTrigger);
				actor.AddCollisionShape(shape);
			}
		}

		material->release();
	}

	struct ColliderData
	{
		byte* Data;
		uint32_t Size;
	};

	std::vector<physx::PxShape*> PXPhysicsWrappers::CreateConvexMesh(MeshColliderComponent& collider, const glm::vec3& scale, bool invalidateOld)
	{
		std::vector<physx::PxShape*> shapes;

		collider.ProcessedMeshes.clear();

		const physx::PxCookingParams& currentParams = s_CookingFactory->getParams();
		physx::PxCookingParams newParams = currentParams;
		newParams.planeTolerance = 0.0f;
		newParams.meshPreprocessParams = physx::PxMeshPreprocessingFlags(physx::PxMeshPreprocessingFlag::eWELD_VERTICES);
		newParams.meshWeldTolerance = 0.01f;
		s_CookingFactory->setParams(newParams);

		if (invalidateOld)
			PhysicsMeshSerializer::DeleteIfSerialized(collider.CollisionMesh->GetFilePath());

		if (!PhysicsMeshSerializer::IsSerialized(collider.CollisionMesh->GetFilePath()))
		{
			const std::vector<Vertex>& vertices = collider.CollisionMesh->GetStaticVertices();
			const std::vector<Index>& indices = collider.CollisionMesh->GetIndices();

			std::unordered_map<std::string, ColliderData> colliderData;
			uint32_t bufferSize = 0;

			for (const auto& submesh : collider.CollisionMesh->GetSubmeshes())
			{
				physx::PxConvexMeshDesc convexDesc;
				convexDesc.points.count = submesh.VertexCount;
				convexDesc.points.stride = sizeof(Vertex);
				convexDesc.points.data = &vertices[submesh.BaseVertex];
				convexDesc.indices.count = submesh.IndexCount / 3;
				convexDesc.indices.data = &indices[submesh.BaseIndex / 3];
				convexDesc.indices.stride = sizeof(Index);
				convexDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eSHIFT_VERTICES;

				physx::PxDefaultMemoryOutputStream buf;
				physx::PxConvexMeshCookingResult::Enum result;
				if (!s_CookingFactory->cookConvexMesh(convexDesc, buf, &result))
				{
					HZ_CORE_ERROR("Failed to cook convex mesh {0}", submesh.MeshName);
					continue;
				}

				ColliderData data;
				data.Size = buf.getSize();
				data.Data = new byte[data.Size];
				memcpy(data.Data, buf.getData(), data.Size);
				colliderData[submesh.MeshName] = data;
				bufferSize += sizeof(uint32_t);
				bufferSize += data.Size;

				glm::vec3 submeshTranslation, submeshRotation, submeshScale;
				Math::DecomposeTransform(submesh.Transform, submeshTranslation, submeshRotation, submeshScale);

				physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
				physx::PxConvexMesh* convexMesh = s_Physics->createConvexMesh(input);
				physx::PxConvexMeshGeometry convexGeometry = physx::PxConvexMeshGeometry(convexMesh, physx::PxMeshScale(ToPhysXVector(submeshScale * scale)));
				convexGeometry.meshFlags = physx::PxConvexMeshGeometryFlag::eTIGHT_BOUNDS;
				physx::PxMaterial* material = s_Physics->createMaterial(0, 0, 0); // Dummy material, will be replaced at runtime.
				physx::PxShape* shape = s_Physics->createShape(convexGeometry, *material, true);
				shape->setLocalPose(ToPhysXTransform(submeshTranslation, submeshRotation));
				shapes.push_back(shape);

				material->release();
				convexMesh->release();
			}

			Buffer colliderBuffer;
			colliderBuffer.Allocate(bufferSize);

			uint32_t offset = 0;
			for (auto& [submeshName, data] : colliderData)
			{
				colliderBuffer.Write(&data.Size, sizeof(uint32_t), offset);
				offset += sizeof(uint32_t);
				colliderBuffer.Write(data.Data, data.Size, offset);
				offset += data.Size;
			
				delete[] data.Data;
			}

			PhysicsMeshSerializer::SerializeMesh(collider.CollisionMesh->GetFilePath(), colliderBuffer);
			colliderBuffer.Release();
		}
		else
		{
			Buffer colliderBuffer = PhysicsMeshSerializer::DeserializeMesh(collider.CollisionMesh->GetFilePath());
			uint32_t offset = 0;

			for (const auto& submesh : collider.CollisionMesh->GetSubmeshes())
			{
				// NOTE(Peter): This way of reading the data requires that the submeshes are always in the same order
				uint32_t dataSize = colliderBuffer.Read<uint32_t>(offset);
				offset += sizeof(uint32_t);
				byte* data = colliderBuffer.ReadBytes(dataSize, offset);
				offset += dataSize;

				glm::vec3 submeshTranslation, submeshRotation, submeshScale;
				Math::DecomposeTransform(submesh.Transform, submeshTranslation, submeshRotation, submeshScale);

				physx::PxDefaultMemoryInputData meshData(data, dataSize);
				physx::PxConvexMesh* convexMesh = s_Physics->createConvexMesh(meshData);
				physx::PxConvexMeshGeometry convexGeometry = physx::PxConvexMeshGeometry(convexMesh, physx::PxMeshScale(ToPhysXVector(submeshScale * scale)));
				convexGeometry.meshFlags = physx::PxConvexMeshGeometryFlag::eTIGHT_BOUNDS;
				physx::PxMaterial* material = s_Physics->createMaterial(0, 0, 0); // Dummy material, will be replaced at runtime.
				physx::PxShape* shape = s_Physics->createShape(convexGeometry, *material, true);
				shape->setLocalPose(ToPhysXTransform(submeshTranslation, submeshRotation));
				shapes.push_back(shape);

				material->release();
				convexMesh->release();

				delete[] data;
			}

			colliderBuffer.Release();
		}

		if (collider.ProcessedMeshes.size() <= 0)
		{
			for (auto shape : shapes)
			{
				physx::PxConvexMeshGeometry convexGeometry;
				shape->getConvexMeshGeometry(convexGeometry);
				physx::PxConvexMesh* mesh = convexGeometry.convexMesh;

				// Based On: https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/ThirdParty/PhysX3/NvCloth/samples/SampleBase/renderer/ConvexRenderMesh.cpp
				const uint32_t nbPolygons = mesh->getNbPolygons();
				const physx::PxVec3* convexVertices = mesh->getVertices();
				const physx::PxU8* convexIndices = mesh->getIndexBuffer();

				uint32_t nbVertices = 0;
				uint32_t nbFaces = 0;

				std::vector<Vertex> collisionVertices;
				std::vector<Index> collisionIndices;
				uint32_t vertCounter = 0;
				uint32_t indexCounter = 0;

				for (uint32_t i = 0; i < nbPolygons; i++)
				{
					physx::PxHullPolygon polygon;
					mesh->getPolygonData(i, polygon);
					nbVertices += polygon.mNbVerts;
					nbFaces += (polygon.mNbVerts - 2) * 3;

					uint32_t vI0 = vertCounter;

					for (uint32_t vI = 0; vI < polygon.mNbVerts; vI++)
					{
						Vertex v;
						v.Position = FromPhysXVector(convexVertices[convexIndices[polygon.mIndexBase + vI]]);
						collisionVertices.push_back(v);
						vertCounter++;
					}

					for (uint32_t vI = 1; vI < uint32_t(polygon.mNbVerts) - 1; vI++)
					{
						Index index;
						index.V1 = uint32_t(vI0);
						index.V2 = uint32_t(vI0 + vI + 1);
						index.V3 = uint32_t(vI0 + vI);
						collisionIndices.push_back(index);
						indexCounter++;
					}

					collider.ProcessedMeshes.push_back(Ref<Mesh>::Create(collisionVertices, collisionIndices, FromPhysXTransform(shape->getLocalPose())));
				}
			}
		}

		s_CookingFactory->setParams(currentParams);
		return shapes;
	}

	std::vector<physx::PxShape*> PXPhysicsWrappers::CreateTriangleMesh(MeshColliderComponent& collider, const glm::vec3& scale, bool invalidateOld)
	{
		std::vector<physx::PxShape*> shapes;

		collider.ProcessedMeshes.clear();

		if (invalidateOld)
			PhysicsMeshSerializer::DeleteIfSerialized(collider.CollisionMesh->GetFilePath());

		if (!PhysicsMeshSerializer::IsSerialized(collider.CollisionMesh->GetFilePath()))
		{
			const std::vector<Vertex>& vertices = collider.CollisionMesh->GetStaticVertices();
			const std::vector<Index>& indices = collider.CollisionMesh->GetIndices();

			std::unordered_map<std::string, ColliderData> colliderData;
			uint32_t bufferSize = 0;

			for (const auto& submesh : collider.CollisionMesh->GetSubmeshes())
			{
				physx::PxTriangleMeshDesc triangleDesc;
				triangleDesc.points.count = submesh.VertexCount;
				triangleDesc.points.stride = sizeof(Vertex);
				triangleDesc.points.data = &vertices[submesh.BaseVertex];
				triangleDesc.triangles.count = submesh.IndexCount / 3;
				triangleDesc.triangles.data = &indices[submesh.BaseIndex / 3];
				triangleDesc.triangles.stride = sizeof(Index);

				physx::PxDefaultMemoryOutputStream buf;
				physx::PxTriangleMeshCookingResult::Enum result;
				if (!s_CookingFactory->cookTriangleMesh(triangleDesc, buf, &result))
				{
					HZ_CORE_ERROR("Failed to cook triangle mesh: {0}", submesh.MeshName);
					continue;
				}

				ColliderData data;
				data.Size = buf.getSize();
				data.Data = new byte[data.Size];
				memcpy(data.Data, buf.getData(), data.Size);
				colliderData[submesh.MeshName] = data;
				bufferSize += sizeof(uint32_t);
				bufferSize += data.Size;

				glm::vec3 submeshTranslation, submeshRotation, submeshScale;
				Math::DecomposeTransform(submesh.Transform, submeshTranslation, submeshRotation, submeshScale);

				physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
				physx::PxTriangleMesh* trimesh = s_Physics->createTriangleMesh(input);
				physx::PxTriangleMeshGeometry triangleGeometry = physx::PxTriangleMeshGeometry(trimesh, physx::PxMeshScale(ToPhysXVector(submeshScale * scale)));
				physx::PxMaterial* material = s_Physics->createMaterial(0, 0, 0); // Dummy material, will be replaced at runtime.
				physx::PxShape* shape = s_Physics->createShape(triangleGeometry, *material, true);
				shape->setLocalPose(ToPhysXTransform(submeshTranslation, submeshRotation));
				shapes.push_back(shape);

				material->release();
				trimesh->release();
			}

			Buffer colliderBuffer;
			colliderBuffer.Allocate(bufferSize);

			uint32_t offset = 0;
			for (auto& [submeshName, data] : colliderData)
			{
				colliderBuffer.Write(&data.Size, sizeof(uint32_t), offset);
				offset += sizeof(uint32_t);
				colliderBuffer.Write(data.Data, data.Size, offset);
				offset += data.Size;

				delete[] data.Data;
			}

			PhysicsMeshSerializer::SerializeMesh(collider.CollisionMesh->GetFilePath(), colliderBuffer);
			colliderBuffer.Release();
		}
		else
		{
			Buffer colliderBuffer = PhysicsMeshSerializer::DeserializeMesh(collider.CollisionMesh->GetFilePath());
			uint32_t offset = 0;

			for (const auto& submesh : collider.CollisionMesh->GetSubmeshes())
			{
				// NOTE(Peter): This way of reading the data requires that the submeshes are always in the same order
				uint32_t dataSize = colliderBuffer.Read<uint32_t>(offset);
				offset += sizeof(uint32_t);
				byte* data = colliderBuffer.ReadBytes(dataSize, offset);
				offset += dataSize;

				glm::vec3 submeshTranslation, submeshRotation, submeshScale;
				Math::DecomposeTransform(submesh.Transform, submeshTranslation, submeshRotation, submeshScale);

				physx::PxDefaultMemoryInputData meshData(data, dataSize);
				physx::PxTriangleMesh* trimesh = s_Physics->createTriangleMesh(meshData);
				physx::PxTriangleMeshGeometry triangleGeometry = physx::PxTriangleMeshGeometry(trimesh, physx::PxMeshScale(ToPhysXVector(submeshScale * scale)));
				physx::PxMaterial* material = s_Physics->createMaterial(0, 0, 0); // Dummy material, will be replaced at runtime.
				physx::PxShape* shape = s_Physics->createShape(triangleGeometry, *material, true);
				shape->setLocalPose(ToPhysXTransform(submeshTranslation, submeshRotation));
				shapes.push_back(shape);

				material->release();
				trimesh->release();

				delete[] data;
			}

			colliderBuffer.Release();
		}

		if (collider.ProcessedMeshes.size() <= 0)
		{
			for (auto shape : shapes)
			{
				physx::PxTriangleMeshGeometry triangleGeometry;
				shape->getTriangleMeshGeometry(triangleGeometry);
				physx::PxTriangleMesh* mesh = triangleGeometry.triangleMesh;

				const uint32_t nbVerts = mesh->getNbVertices();
				const physx::PxVec3* triangleVertices = mesh->getVertices();
				const uint32_t nbTriangles = mesh->getNbTriangles();
				const physx::PxU16* tris = (const physx::PxU16*)mesh->getTriangles();

				std::vector<Vertex> vertices;
				std::vector<Index> indices;

				for (uint32_t v = 0; v < nbVerts; v++)
				{
					Vertex v1;
					v1.Position = FromPhysXVector(triangleVertices[v]);
					vertices.push_back(v1);
				}

				for (uint32_t tri = 0; tri < nbTriangles; tri++)
				{
					Index index;
					index.V1 = tris[3 * tri + 0];
					index.V2 = tris[3 * tri + 1];
					index.V3 = tris[3 * tri + 2];
					indices.push_back(index);
				}

				glm::mat4 scale = glm::scale(glm::mat4(1.0f), FromPhysXVector(triangleGeometry.scale.scale));
				glm::mat4 transform = FromPhysXTransform(shape->getLocalPose()) * scale;
				collider.ProcessedMeshes.push_back(Ref<Mesh>::Create(vertices, indices, transform));
			}
		}

		return shapes;
	}

	bool PXPhysicsWrappers::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit* hit)
	{
		physx::PxScene* scene = static_cast<physx::PxScene*>(Physics::GetPhysicsScene());
		physx::PxRaycastBuffer hitInfo;
		bool result = scene->raycast(ToPhysXVector(origin), ToPhysXVector(glm::normalize(direction)), maxDistance, hitInfo);

		if (result)
		{
			Entity& entity = *(Entity*)hitInfo.block.actor->userData;
			hit->EntityID = entity.GetUUID();
			hit->Position = FromPhysXVector(hitInfo.block.position);
			hit->Normal = FromPhysXVector(hitInfo.block.normal);
			hit->Distance = hitInfo.block.distance;
		}

		return result;
	}

	bool PXPhysicsWrappers::OverlapBox(const glm::vec3& origin, const glm::vec3& halfSize, std::array<physx::PxOverlapHit, OVERLAP_MAX_COLLIDERS>& buffer, uint32_t* count)
	{
		physx::PxScene* scene = static_cast<physx::PxScene*>(Physics::GetPhysicsScene());

		memset(s_OverlapBuffer, 0, sizeof(s_OverlapBuffer));
		physx::PxOverlapBuffer buf(s_OverlapBuffer, OVERLAP_MAX_COLLIDERS);
		physx::PxBoxGeometry geometry = physx::PxBoxGeometry(halfSize.x, halfSize.y, halfSize.z);
		physx::PxTransform pose = ToPhysXTransform(glm::translate(glm::mat4(1.0f), origin));

		bool result = scene->overlap(geometry, pose, buf);

		if (result)
		{
			uint32_t bodyCount = buf.nbTouches >= OVERLAP_MAX_COLLIDERS ? OVERLAP_MAX_COLLIDERS : buf.nbTouches;
			memcpy(buffer.data(), buf.touches, bodyCount * sizeof(physx::PxOverlapHit));
			*count = bodyCount;
		}

		return result;
	}

	bool PXPhysicsWrappers::OverlapCapsule(const glm::vec3& origin, float radius, float halfHeight, std::array<physx::PxOverlapHit, OVERLAP_MAX_COLLIDERS>& buffer, uint32_t* count)
	{
		physx::PxScene* scene = static_cast<physx::PxScene*>(Physics::GetPhysicsScene());

		memset(s_OverlapBuffer, 0, sizeof(s_OverlapBuffer));
		physx::PxOverlapBuffer buf(s_OverlapBuffer, OVERLAP_MAX_COLLIDERS);
		physx::PxCapsuleGeometry geometry = physx::PxCapsuleGeometry(radius, halfHeight);
		physx::PxTransform pose = ToPhysXTransform(glm::translate(glm::mat4(1.0f), origin));

		bool result = scene->overlap(geometry, pose, buf);

		if (result)
		{
			uint32_t bodyCount = buf.nbTouches >= OVERLAP_MAX_COLLIDERS ? OVERLAP_MAX_COLLIDERS : buf.nbTouches;
			memcpy(buffer.data(), buf.touches, bodyCount * sizeof(physx::PxOverlapHit));
			*count = bodyCount;
		}

		return result;
	}

	bool PXPhysicsWrappers::OverlapSphere(const glm::vec3& origin, float radius, std::array<physx::PxOverlapHit, OVERLAP_MAX_COLLIDERS>& buffer, uint32_t* count)
	{
		physx::PxScene* scene = static_cast<physx::PxScene*>(Physics::GetPhysicsScene());

		memset(s_OverlapBuffer, 0, sizeof(s_OverlapBuffer));
		physx::PxOverlapBuffer buf(s_OverlapBuffer, OVERLAP_MAX_COLLIDERS);
		physx::PxSphereGeometry geometry = physx::PxSphereGeometry(radius);
		physx::PxTransform pose = ToPhysXTransform(glm::translate(glm::mat4(1.0f), origin));

		bool result = scene->overlap(geometry, pose, buf);

		if (result)
		{
			uint32_t bodyCount = buf.nbTouches >= OVERLAP_MAX_COLLIDERS ? OVERLAP_MAX_COLLIDERS : buf.nbTouches;
			memcpy(buffer.data(), buf.touches, bodyCount * sizeof(physx::PxOverlapHit));
			*count = bodyCount;
		}

		return result;
	}

	physx::PxPhysics& PXPhysicsWrappers::GetPhysics()
	{
		return *s_Physics;
	}

	void PXPhysicsWrappers::Initialize()
	{
		HZ_CORE_ASSERT(!s_Foundation, "PXPhysicsWrappers::Initializer shouldn't be called more than once!");

		s_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, s_Allocator, s_ErrorCallback);
		HZ_CORE_ASSERT(s_Foundation, "PxCreateFoundation Failed!");

		s_PVD = PxCreatePvd(*s_Foundation);
		if (s_PVD)
		{
			physx::PxPvdTransport* transport = physx::PxDefaultPvdSocketTransportCreate("localhost", 5425, 10);
			s_PVD->connect(*transport, physx::PxPvdInstrumentationFlag::eALL);
		}

		physx::PxTolerancesScale scale = physx::PxTolerancesScale();
		scale.length = 10;
		s_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *s_Foundation, scale, true, s_PVD);
		HZ_CORE_ASSERT(s_Physics, "PxCreatePhysics Failed!");

		s_CookingFactory = PxCreateCooking(PX_PHYSICS_VERSION, *s_Foundation, s_Physics->getTolerancesScale());
		HZ_CORE_ASSERT(s_CookingFactory, "PxCreatePhysics Failed!");

		PxSetAssertHandler(s_AssertHandler);
	}

	void PXPhysicsWrappers::Shutdown()
	{
		if (s_CPUDispatcher)
			s_CPUDispatcher->release();
		s_CPUDispatcher = nullptr;

		if (s_CookingFactory)
			s_CookingFactory->release();
		s_CookingFactory = nullptr;

		if (s_Foundation)
			s_Physics->release();
		s_Physics = nullptr;

		if (s_Foundation)
			s_Foundation->release();
		s_Foundation = nullptr;
	}

	physx::PxAllocatorCallback& PXPhysicsWrappers::GetAllocator()
	{
		return s_Allocator;
	}

	void PhysicsAssertHandler::operator()(const char* exp, const char* file, int line, bool& ignore)
	{
		HZ_CORE_ERROR("[PhysX Error]: {0}:{1} - {2}", file, line, exp);

#if 0
		HZ_CORE_ASSERT(false);
#endif
	}

}
