﻿//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Optional/XUSGObjLoader.h"
#include "SoftGraphicsPipeline.h"

using namespace std;
using namespace DirectX;
using namespace XUSG;

const uint32_t MAX_PIXEL_COUNT = (UINT32_MAX >> 4) + 1;
const uint32_t MAX_VERTEX_COUNT = (UINT32_MAX >> 8) + 1;

SoftGraphicsPipeline::SoftGraphicsPipeline(const Device& device) :
	m_device(device),
	m_pColorTarget(nullptr)
{
	m_computePipelineCache.SetDevice(device);
	m_descriptorTableCache.SetDevice(device);
	m_pipelineLayoutCache.SetDevice(device);
}

SoftGraphicsPipeline::~SoftGraphicsPipeline()
{
}

bool SoftGraphicsPipeline::Init(const CommandList& commandList, uint32_t width, uint32_t height,
	vector<Resource>& uploaders)
{
	m_viewport.x = static_cast<float>(width);
	m_viewport.y = static_cast<float>(height);

	// Create buffers
	N_RETURN(m_tilePrimCount.Create(m_device, 3, sizeof(uint32_t), ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, ResourceState::COMMON, 1, nullptr, 1, nullptr, L"TilePrimitiveCount"), false);
	N_RETURN(m_tiledPrimitives.Create(m_device, MAX_PIXEL_COUNT, sizeof(uint32_t[2]), ResourceFlag::ALLOW_UNORDERED_ACCESS,
		MemoryType::DEFAULT, ResourceState::COMMON, 1, nullptr, 1, nullptr, L"TiledPrimitives"), false);
	N_RETURN(m_vertexPos.Create(m_device, MAX_VERTEX_COUNT, sizeof(float[4]), Format::R32G32B32A32_FLOAT,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, ResourceState::COMMON, 1,
		nullptr, 1, nullptr, L"VertexPositions"), false);

	// create reset buffer for resetting TilePrimitiveCount
	N_RETURN(createResetBuffer(commandList, uploaders), false);

	// create command layout
	N_RETURN(createCommandLayout(), false);

	return true;
}

bool SoftGraphicsPipeline::CreateVertexShaderLayout(Util::PipelineLayout& pipelineLayout, uint32_t slotCount)
{
	m_extVsTables.resize(slotCount);
	const auto numUAVs = static_cast<uint32_t>(m_vertexAttribs.size()) + 1;
	auto pipelineLayoutIndexed = pipelineLayout;

	// Create pipeline layouts
	{
		pipelineLayout.SetRange(slotCount, DescriptorType::SRV, 1, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayout.SetRange(slotCount + 1, DescriptorType::UAV, numUAVs, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[VERTEX_PROCESS], pipelineLayout.GetPipelineLayout(
			m_pipelineLayoutCache, PipelineLayoutFlag::NONE, L"VertexShaderStageLayout"), false);
	}

	{
		pipelineLayoutIndexed.SetRange(slotCount, DescriptorType::SRV, 2, 0, 0, DescriptorRangeFlag::DATA_STATIC);
		pipelineLayoutIndexed.SetRange(slotCount + 1, DescriptorType::UAV, numUAVs, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[VERTEX_INDEXED], pipelineLayoutIndexed.GetPipelineLayout(
			m_pipelineLayoutCache, PipelineLayoutFlag::NONE, L"VertexShaderStageIndexedLayout"), false);
	}

	// Create pipelines
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, VERTEX_PROCESS, L"VSStage.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[VERTEX_PROCESS]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, VERTEX_PROCESS));
		X_RETURN(m_pipelines[VERTEX_PROCESS], state.GetPipeline(m_computePipelineCache, L"VertexShaderStage"), false);
	}

	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, VERTEX_INDEXED, L"VSStageIndexed.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[VERTEX_INDEXED]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, VERTEX_INDEXED));
		X_RETURN(m_pipelines[VERTEX_INDEXED], state.GetPipeline(m_computePipelineCache, L"VertexShaderStageIndexed"), false);
	}

	N_RETURN(createPipelines(), false);

	// create descriptor tables
	N_RETURN(createDescriptorTables(), false);

	return true;
}

bool SoftGraphicsPipeline::SetAttribute(uint32_t i, uint32_t stride, Format format, const wchar_t* name)
{
	if (i >= m_vertexAttribs.size()) m_vertexAttribs.resize(i + 1);

	return m_vertexAttribs[i].Create(m_device, MAX_VERTEX_COUNT, stride, format,
		ResourceFlag::ALLOW_UNORDERED_ACCESS, MemoryType::DEFAULT, ResourceState::COMMON, 1,
		nullptr, 1, nullptr, name);
}

void SoftGraphicsPipeline::SetVertexBuffer(const Descriptor& vertexBufferView)
{
	m_vertexBufferView = vertexBufferView;
}

void SoftGraphicsPipeline::SetIndexBuffer(const Descriptor& indexBufferView)
{
	m_indexBufferView = indexBufferView;
}

void SoftGraphicsPipeline::SetRenderTargets(Texture2D* pColorTarget, Texture2D* pDepth)
{
	m_pColorTarget = pColorTarget;
	m_pDepth = pDepth;

	m_outTables.resize(pDepth ? 2 : 1);
	{
		Util::DescriptorTable utilUavTable;
		utilUavTable.SetDescriptors(0, 1, &pColorTarget->GetUAV());
		m_outTables[0] = utilUavTable.GetCbvSrvUavTable(m_descriptorTableCache);
	}

	if (pDepth)
	{
		Util::DescriptorTable utilUavTable;
		utilUavTable.SetDescriptors(0, 1, &pDepth->GetUAV());
		m_outTables[m_outTables.size() - 1] = utilUavTable.GetCbvSrvUavTable(m_descriptorTableCache);
	}
}

void SoftGraphicsPipeline::VSSetDescriptorTable(uint32_t i, const XUSG::DescriptorTable& descriptorTable)
{
	m_extVsTables[i] = descriptorTable;
}

void SoftGraphicsPipeline::Clear(const XUSG::Texture2D& target, const float clearValues[4], bool asUint)
{
	m_clears.emplace_back();
	m_clears.back().IsUint = asUint;
	m_clears.back().pTarget = &target;
	memcpy(m_clears.back().ClearUint, clearValues, sizeof(float[4]));
}

void SoftGraphicsPipeline::Draw(CommandList& commandList, uint32_t numVertices)
{
	Util::DescriptorTable utilSrvTable;
	const Descriptor descriptors[] =
	{
		m_vertexBufferView
	};
	utilSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
	m_srvTables[SRV_TABLE_VS] = utilSrvTable.GetCbvSrvUavTable(m_descriptorTableCache);

	draw(commandList, numVertices, VERTEX_PROCESS);
}

void SoftGraphicsPipeline::DrawIndexed(CommandList& commandList, uint32_t numIndices)
{
	Util::DescriptorTable utilSrvTable;
	const Descriptor descriptors[] =
	{
		m_vertexBufferView,
		m_indexBufferView
	};
	utilSrvTable.SetDescriptors(0, static_cast<uint32_t>(size(descriptors)), descriptors);
	m_srvTables[SRV_TABLE_VS] = utilSrvTable.GetCbvSrvUavTable(m_descriptorTableCache);

	draw(commandList, numIndices, VERTEX_INDEXED);
}

bool SoftGraphicsPipeline::CreateVertexBuffer(const CommandList& commandList,
	VertexBuffer& vb, vector<Resource>& uploaders, const void* pData,
	uint32_t numVert, uint32_t srtide, const wchar_t* name) const
{
	N_RETURN(vb.Create(m_device, numVert, srtide, ResourceFlag::NONE,
		MemoryType::DEFAULT, ResourceState::COPY_DEST, 1, nullptr,
		1, nullptr, 1, nullptr, name), false);
	uploaders.push_back(nullptr);

	return vb.Upload(commandList, uploaders.back(), pData, srtide * numVert,
		ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

bool SoftGraphicsPipeline::CreateIndexBuffer(const CommandList& commandList,
	IndexBuffer& ib,vector<Resource>& uploaders, const void* pData,
	uint32_t numIdx, Format format, const wchar_t* name) const
{
	assert(format == Format::R16_UINT || format == Format::R32_UINT);
	const uint32_t byteWidth = (format == Format::R16_UINT ? sizeof(uint16_t) : sizeof(uint32_t)) * numIdx;
	N_RETURN(ib.Create(m_device, byteWidth, format, ResourceFlag::NONE,
		MemoryType::DEFAULT, ResourceState::COPY_DEST, 1, nullptr,
		1, nullptr, 1, nullptr, name), false);
	uploaders.push_back(nullptr);

	return ib.Upload(commandList, uploaders.back(), pData, byteWidth,
		ResourceState::NON_PIXEL_SHADER_RESOURCE);
}

DescriptorTableCache& SoftGraphicsPipeline::GetDescriptorTableCache()
{
	return m_descriptorTableCache;
}

bool SoftGraphicsPipeline::createPipelines()
{
	// Create pipeline layouts
	{
		Util::PipelineLayout utilPipelineLayout;
		utilPipelineLayout.SetConstants(0, SizeOfInUint32(CBViewPort), 0);
		utilPipelineLayout.SetRange(1, DescriptorType::SRV, 1, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		utilPipelineLayout.SetRange(2, DescriptorType::UAV, 2, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[BIN_RASTER], utilPipelineLayout.GetPipelineLayout(
			m_pipelineLayoutCache, PipelineLayoutFlag::NONE, L"BinRasterLayout"), false);
	}

	{
		Util::PipelineLayout utilPipelineLayout;
		const auto numSRVs = static_cast<uint32_t>(m_vertexAttribs.size()) + 2;
		utilPipelineLayout.SetConstants(0, SizeOfInUint32(CBViewPort), 0);
		utilPipelineLayout.SetRange(1, DescriptorType::SRV, numSRVs, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		utilPipelineLayout.SetRange(2, DescriptorType::UAV, 2, 0, 0,
			DescriptorRangeFlag::DATA_STATIC_WHILE_SET_AT_EXECUTE);
		X_RETURN(m_pipelineLayouts[PIX_RASTER], utilPipelineLayout.GetPipelineLayout(
			m_pipelineLayoutCache, PipelineLayoutFlag::NONE, L"PixelRasterLayout"), false);
	}

	// Create compute pipelines
	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, BIN_RASTER, L"BinRaster.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[BIN_RASTER]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, BIN_RASTER));
		X_RETURN(m_pipelines[BIN_RASTER], state.GetPipeline(m_computePipelineCache, L"BinRaster"), false);
	}

	{
		N_RETURN(m_shaderPool.CreateShader(Shader::Stage::CS, PIX_RASTER, L"PixelRaster.cso"), false);

		Compute::State state;
		state.SetPipelineLayout(m_pipelineLayouts[PIX_RASTER]);
		state.SetShader(m_shaderPool.GetShader(Shader::Stage::CS, PIX_RASTER));
		X_RETURN(m_pipelines[PIX_RASTER], state.GetPipeline(m_computePipelineCache, L"BinRaster"), false);
	}

	return true;
}

bool SoftGraphicsPipeline::createResetBuffer(const CommandList& commandList, vector<Resource>& uploaders)
{
	N_RETURN(m_tilePrimCountReset.Create(m_device, 1, sizeof(uint32_t),
		ResourceFlag::NONE, MemoryType::DEFAULT, ResourceState::COMMON,
		1, nullptr, 1, nullptr, L"TilePrimitiveCountReset"), false);

	const uint32_t pDataReset[] = { 0, 1, 1 };
	uploaders.push_back(nullptr);
	N_RETURN(m_tilePrimCount.Upload(commandList, uploaders.back(),
		pDataReset, sizeof(uint32_t[3]), ResourceState::COPY_SOURCE), false);

	uploaders.push_back(nullptr);

	return m_tilePrimCountReset.Upload(commandList, uploaders.back(),
		pDataReset, sizeof(uint32_t), ResourceState::COPY_SOURCE);
}

bool SoftGraphicsPipeline::createCommandLayout()
{
	IndirectArgument arg;
	arg.Type = IndirectArgumentType::DISPATCH;

	return m_device->CreateCommandLayout(m_commandLayout, sizeof(uint32_t[3]), 1, &arg);
}

bool SoftGraphicsPipeline::createDescriptorTables()
{
	const auto numAttribs = static_cast<uint32_t>(m_vertexAttribs.size());

	Util::DescriptorTable utilSrvTable;
	vector<Descriptor> srvs;
	srvs.reserve(numAttribs + 2);
	srvs.push_back(m_vertexPos.GetSRV());
	srvs.push_back(m_tiledPrimitives.GetSRV());
	for (const auto& attrib : m_vertexAttribs) srvs.push_back(attrib.GetSRV());
	utilSrvTable.SetDescriptors(0, static_cast<uint32_t>(srvs.size()), srvs.data());
	X_RETURN(m_srvTables[SRV_TABLE_RASTER], utilSrvTable.GetCbvSrvUavTable(m_descriptorTableCache), false);

	{
		Util::DescriptorTable utilUavTable;
		vector<Descriptor> uavs;
		uavs.reserve(numAttribs + 1);
		uavs.push_back(m_vertexPos.GetUAV());
		for (const auto& attrib : m_vertexAttribs) uavs.push_back(attrib.GetUAV());
		utilUavTable.SetDescriptors(0, static_cast<uint32_t>(uavs.size()), uavs.data());
		X_RETURN(m_uavTables[UAV_TABLE_VS], utilUavTable.GetCbvSrvUavTable(m_descriptorTableCache), false);
	}

	{
		Util::DescriptorTable utilUavTable;
		const Descriptor uavs[] =
		{
			m_tilePrimCount.GetUAV(),
			m_tiledPrimitives.GetUAV()
		};
		utilUavTable.SetDescriptors(0, static_cast<uint32_t>(size(uavs)), uavs);
		X_RETURN(m_uavTables[UAV_TABLE_BIN], utilUavTable.GetCbvSrvUavTable(m_descriptorTableCache), false);
	}

	return true;
}

void SoftGraphicsPipeline::draw(CommandList& commandList, uint32_t num, StageIndex vs)
{
	const DescriptorPool descriptorPools[] =
	{
		m_descriptorTableCache.GetDescriptorPool(CBV_SRV_UAV_POOL),
		//m_descriptorTableCache.GetDescriptorPool(SAMPLER_POOL)
	};
	commandList.SetDescriptorPools(static_cast<uint32_t>(size(descriptorPools)), descriptorPools);

	// Set resource barriers
	vector<ResourceBarrier> barriers(m_vertexAttribs.size() + 2);
	auto numBarriers = m_tilePrimCount.SetBarrier(barriers.data(), ResourceState::COPY_DEST);
	numBarriers = m_vertexPos.SetBarrier(barriers.data(), ResourceState::UNORDERED_ACCESS, numBarriers);
	for (auto& attrib : m_vertexAttribs)
		numBarriers = attrib.SetBarrier(barriers.data(), ResourceState::UNORDERED_ACCESS, numBarriers);
	commandList.Barrier(numBarriers, barriers.data());

	// Vertex shader
	{
		// Set descriptor tables
		const auto baseIdx = static_cast<uint32_t>(m_extVsTables.size());
		commandList.SetComputePipelineLayout(m_pipelineLayouts[vs]);
		for (auto i = 0u; i < baseIdx; ++i)
			commandList.SetComputeDescriptorTable(i, m_extVsTables[i]);
		commandList.SetComputeDescriptorTable(baseIdx, m_srvTables[SRV_TABLE_VS]);
		commandList.SetComputeDescriptorTable(baseIdx + 1, m_uavTables[UAV_TABLE_VS]);

		// Set pipeline state
		commandList.SetPipelineState(m_pipelines[vs]);

		// Dispatch
		commandList.Dispatch(DIV_UP(num, 64), 1, 1);
	}

	// Reset TilePrimitiveCount
	rasterizer(commandList, num / 3);
}

void SoftGraphicsPipeline::rasterizer(CommandList& commandList, uint32_t numTriangles)
{
	CBViewPort cbViewport;
	cbViewport.x = 0.0f;
	cbViewport.y = 0.0f;
	cbViewport.w = m_viewport.x;
	cbViewport.h = m_viewport.y;
	cbViewport.numTileX = static_cast<uint32_t>(ceil(cbViewport.w / 8.0f));
	cbViewport.numTileY = static_cast<uint32_t>(ceil(cbViewport.h / 8.0f));

	// Reset TilePrimitiveCount
	commandList.CopyBufferRegion(m_tilePrimCount.GetResource(), 0,
		m_tilePrimCountReset.GetResource(), 0, sizeof(uint32_t));

	// Set resource barriers
	vector<ResourceBarrier> barriers(m_vertexAttribs.size() + 3);
	auto numBarriers = m_vertexPos.SetBarrier(barriers.data(), ResourceState::NON_PIXEL_SHADER_RESOURCE);
	numBarriers = m_tilePrimCount.SetBarrier(barriers.data(), ResourceState::UNORDERED_ACCESS, numBarriers);
	numBarriers = m_tiledPrimitives.SetBarrier(barriers.data(), ResourceState::UNORDERED_ACCESS, numBarriers);
	commandList.Barrier(numBarriers, barriers.data());

	// Bin raster
	{
		// Set descriptor tables
		commandList.SetComputePipelineLayout(m_pipelineLayouts[BIN_RASTER]);
		commandList.SetCompute32BitConstants(0, SizeOfInUint32(cbViewport), &cbViewport);
		commandList.SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_RASTER]);
		commandList.SetComputeDescriptorTable(2, m_uavTables[UAV_TABLE_BIN]);

		// Set pipeline state
		commandList.SetPipelineState(m_pipelines[BIN_RASTER]);

		// Dispatch
		commandList.Dispatch(DIV_UP(numTriangles, 64), 1, 1);
	}

	// Set resource barriers
	numBarriers = m_pColorTarget->SetBarrier(barriers.data(), ResourceState::UNORDERED_ACCESS);
	numBarriers = m_tilePrimCount.SetBarrier(barriers.data(), ResourceState::INDIRECT_ARGUMENT, numBarriers);
	numBarriers = m_tiledPrimitives.SetBarrier(barriers.data(), ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	for (auto& attrib : m_vertexAttribs)
		numBarriers = attrib.SetBarrier(barriers.data(), ResourceState::NON_PIXEL_SHADER_RESOURCE, numBarriers);
	commandList.Barrier(numBarriers, barriers.data());

	// Clear
	const uint32_t clearCount = static_cast<uint32_t>(m_clears.size());
	for (auto i = 0u; i < clearCount; ++i)
	{
		if (m_clears[i].IsUint)
			commandList.ClearUnorderedAccessViewUint(m_outTables[i], m_clears[i].pTarget->GetUAV(),
				m_clears[i].pTarget->GetResource(), m_clears[i].ClearUint);
		else commandList.ClearUnorderedAccessViewFloat(m_outTables[i], m_clears[i].pTarget->GetUAV(),
			m_clears[i].pTarget->GetResource(), m_clears[i].ClearFloat);
	}
	m_clears.clear();

	// Pixel raster
	{
		// Set descriptor tables
		commandList.SetComputePipelineLayout(m_pipelineLayouts[PIX_RASTER]);
		commandList.SetCompute32BitConstants(0, SizeOfInUint32(cbViewport), &cbViewport);
		commandList.SetComputeDescriptorTable(1, m_srvTables[SRV_TABLE_RASTER]);
		commandList.SetComputeDescriptorTable(2, m_outTables[0]);

		// Set pipeline state
		commandList.SetPipelineState(m_pipelines[PIX_RASTER]);

		// Dispatch indirect
		commandList.ExecuteIndirect(m_commandLayout, 1, m_tilePrimCount.GetResource(),
			0, m_tilePrimCount.GetResource());
	}
}
