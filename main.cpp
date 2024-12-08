#include <Windows.h>
#include <cstdint>		/*int32_tを使うためにincludeを追加*/
#include <string>
#include <format>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <dxcapi.h>
#include <fstream>
#include <sstream>
#include <wrl.h>
#include <random>
#include <numbers>

#include "externals/DirectXTex/DirectXTex.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "ResourceObject.h"

#include "Vector2.h"
#include "Vector4.h"
#include "Matrix4x4.h"
//#include "VectorMath.h"
#include "MatrixMath.h"
#include "Transform.h"
#include "VertexData.h"
#include "Material.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include "Particle.h"
#include "ParticleForGPU.h"
#include "Emitter.h"
#include "AccelerationField.h"

#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

#define pi 3.141592653589793238462643383279502884197169399375105820974944f

//クライアント領域サイズ
const int32_t kClientWidth = 1280;
const int32_t kClientHeight = 720;

struct WindZone
{
	AABB area; // 風が吹く範囲
	Vector3 strength; // 風の強さ
};

enum BlendMode
{
	kBlendModeNone,		// ブレンドなし
	kBlendModeNormal,	// 通常αブレンド、デフォルト。Src * srcA + Dest * (1 - SrcA)
	kBlendModeAdd,		// 加算 Src * SrcA + Dest * 1
	kBlendModeSubtract,	// 減算 Dest * 1 - Src * SrcA
	kBlendModeMultiply, // 乗算 Src * 0 + Dest * Src
	kBlendModeScreen,	// スクリーン Src * (1 - Dest) + Dest * 1
	kcountOfBlendMode,	// 利用してはいけない
};

BlendMode currentBlendMode = kBlendModeAdd;

const char* blendModeNames[kcountOfBlendMode] = {
	"kBlendModeNone",        // ブレンドなし
	"kBlendModeNormal",      // 通常αブレンド、デフォルト
	"kBlendModeAdd",         // 加算
	"kBlendModeSubtract",    // 減算
	"kBlendModeMultiply",    // 乗算
	"kBlendModeScreen"       // スクリーン
};

// comptrの構造体
struct D3DResourceLeakChecker
{
	~D3DResourceLeakChecker()
	{
		// リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
		{
			//ライブオブジェクトの報告
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
			//デバッグインターフェースの解放
			debug->Release();
		}
	}
};

// MaterialDataの構造体
struct MaterialData
{
	std::string textureFilePath;
};

// ModelData構造体
struct ModelData
{
	std::vector<VertexData> vertices;
	MaterialData material;
};

//ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
	{
		return true;
	}

	//メッセージに応じてゲーム固有の処理を行う
	switch (msg)
	{
		//ウィンドウが破棄された
	case WM_DESTROY:
		//OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	//標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// 出力ウィンドウに文字を出す
static void Log(const std::string& message)
{
	OutputDebugStringA(message.c_str());
}

// 出力ウィンドウに文字を出す
//string->wstring
std::wstring ConvertString(const std::string& str)
{
	if (str.empty())
	{
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0)
	{
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}
//wstring->string
std::string ConvertString(const std::wstring& str)
{
	if (str.empty())
	{
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0)
	{
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

// DescriptorHeapを生成する
Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> CreateDescriptorHeap(Microsoft::WRL::ComPtr <ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shadervisible)
{
	//ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;	//レンダーターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescriptors;						//ダブルバッファ用に2つ。多くても別に構わない
	descriptorHeapDesc.Flags = shadervisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	//ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

// Resource作成の関数化
Microsoft::WRL::ComPtr <ID3D12Resource> CreateBufferResource(Microsoft::WRL::ComPtr <ID3D12Device> device, size_t sizeInBytes)
{
	//頂点リソース用のヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;	//UploadHeapを使う
	//頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	//バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes;				//リソースのサイズ
	//バッファの場合はこれらは1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	//バッファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	//実際に頂点リソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexResource));
	assert(SUCCEEDED(hr));
	return vertexResource.Get();
}

// CompilerShader関数
IDxcBlob* CompilerShader(
	//CompilerするShaderファイルへのパス
	const std::wstring& filePath,
	//Compilerに使用するProfile
	const wchar_t* profile,
	//初期化で生成したものを3つ
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler)
{
	//これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
	//hlslファイルを読み込む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	//読めなかったら止める
	assert(SUCCEEDED(hr));
	//読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;	//UTF8の文字コードであることを通知

	/// 2.Compileする
	LPCWSTR arguments[] =
	{
		filePath.c_str(),			//コンパイル対象のhlslファイル名
		L"-E",L"main",				//エントリーポイントの指定。基本的にmain以外にはしない
		L"-T",profile,				//ShaderProfileの設定
		L"-Zi",L"-Qembed_debug",	//デバッグ用の情報を詰め込む
		L"-Od",						//最適化を外しておく
		L"-Zpr",					//メモリレイアウトは行優先
	};
	//実際にSahaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,		//読み込んだファイル
		arguments,					//コンパイルオプション
		_countof(arguments),		//コンパイルオプションの数
		includeHandler,				//includeが服待てた諸々
		IID_PPV_ARGS(&shaderResult)	//コンパイル結果
	);
	//コンパイルエラーではなくdxcが起動できないなどの致命的な状況
	assert(SUCCEEDED(hr));

	// 3.警告・エラーが出てないか確認する
	//警告・エラーが出てきたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0)
	{
		Log(shaderError->GetStringPointer());

		//警告・エラーダメゼッタイ
		assert(false);
	}

	// 4.Compile結果を受け取って返す
	//コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	//成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	assert(SUCCEEDED(hr));
	//もう使わないリソースを解放
	shaderSource->Release();
	shaderResult->Release();
	//実行用のバイナリを返却
	return shaderBlob;
}

// Textureデータを読む
DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	//テクスチャファイルを呼んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	//ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	//ミップマップ付きのデータを返す
	return mipImages;
}

// DirectX12のTextureResourceを作る
Microsoft::WRL::ComPtr <ID3D12Resource> CreateTextureResource(Microsoft::WRL::ComPtr <ID3D12Device> device, const DirectX::TexMetadata& metadata)
{
	//1. metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);									//Textureの幅
	resourceDesc.Height = UINT(metadata.height);								//Textureの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);						//mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);					//奥行 or 配列Textureの配列行数
	resourceDesc.Format = metadata.format;										//TextureのFormat
	resourceDesc.SampleDesc.Count = 1;											//サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);		//Textureの次元数。普段使っているのは二次元

	//2. 利用するHeapの設定。非常に特殊な運用。02_04exで一般的なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;								//細かい設定を行う
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;		//WriteBackポリシーでCPUアクセス可能
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;					//プロセッサの近くに配膳

	//3. Resourceを生成する
	Microsoft::WRL::ComPtr <ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,														//Heapの設定
		D3D12_HEAP_FLAG_NONE,													//Heapの特殊な設定。特になし。
		&resourceDesc,															///Resourceの設定
		D3D12_RESOURCE_STATE_GENERIC_READ,										//初回のResourceState。Textureは基本読むだけ
		nullptr,																//Clear最適値。使わないのでnullptr
		IID_PPV_ARGS(&resource));												//作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

//データを移送するUploadTextureData関数
void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
{
	//Meta情報を取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	//全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel)
	{
		//MipMapLevelを指定して各Imageを取得
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
		//Textureに転送
		HRESULT hr = texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,				//全領域へコピー
			img->pixels,			//元データアドレス
			UINT(img->rowPitch),	//1ラインサイズ
			UINT(img->slicePitch)	//1枚サイズ
		);
		assert(SUCCEEDED(hr));
	}
}

// DepthStencilTextureを作る
Microsoft::WRL::ComPtr <ID3D12Resource> CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr <ID3D12Device> device, int32_t width, int32_t height)
{
	//生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;										//Textureの幅
	resourceDesc.Height = height;									//Textureの高さ
	resourceDesc.MipLevels = 1;										//mipmapの数
	resourceDesc.DepthOrArraySize = 1;								//奥行 or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;			//DepthStencilとして知用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;								//サンプリングカウント。１固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;	//２次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;	//DepthStencilとして使う通知

	//利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapPropaties{};
	heapPropaties.Type = D3D12_HEAP_TYPE_DEFAULT;					//VRAMに作る

	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;					//1.0f（最大値）でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;		//フォーマット。Resourceと合わせる

	//Resourceの生成
	Microsoft::WRL::ComPtr <ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapPropaties,							//Heapの設定
		D3D12_HEAP_FLAG_NONE,					//Heapの特殊な設定。特になし。
		&resourceDesc,							//Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,		//深度値を書き込む状態にしておく
		&depthClearValue,						//Clear最適地
		IID_PPV_ARGS(&resource));				//作成するResourceポインタのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

//CPUのDescriptorHandleを取得する関数
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

//GPUのDescriptorHandleを取得する関数
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	//1. 中で必要なる変数の宣言
	MaterialData materialData;
	std::string line;

	//2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename); //ファイルを開く
	assert(file.is_open());// とりあえず開けなかったら止める

	//3. 実際にファイルを読み、ModelDataを構築していく_頂点情報を読む
	while (std::getline(file, line))
	{
		std::string identifire;
		std::istringstream s(line);
		s >> identifire;

		// identifireに応じた処理
		if (identifire == "map_Kd")
		{
			std::string textureFilename;
			s >> textureFilename;
			//連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	//4. MaterialDataを返す
	return materialData;
}

// Objファイルを読み込む関数
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	//1. 中で必要なる変数の宣言
	ModelData modelData;				// 構築するModelData
	std::vector<Vector4> positions;		// 位置
	std::vector<Vector3> normals;		// 法線
	std::vector<Vector2> texcoords;		// テクスチャ座標
	std::string line;					// ファイルから読んだ1行を格納するもの

	//2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);		// ファイルを開く
	assert(file.is_open());									// 開けなかったら止める

	//3. 実際にファイルを読み、ModelDataを構築していく_頂点情報を読む
	while (std::getline(file, line))
	{
		std::string identifier;
		std::stringstream s(line);
		s >> identifier;			// 先頭の識別子を読む

		// identifierに応じた処理
		if (identifier == "v")
		{
			Vector4 position{};
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vt")
		{
			Vector2 texcoord{};
			s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);
		}
		else if (identifier == "vn")
		{
			Vector3 normal{};
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		}
		else if (identifier == "f")
		{
			VertexData triangle[3];
			// 三角形限定。その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
			{
				std::string vertexDefinition;
				s >> vertexDefinition;
				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndieces[3]{};
				for (int32_t element = 0; element < 3; ++element)
				{
					std::string index;
					std::getline(v, index, '/');	// 区切りでインデックスを読んでいく
					elementIndieces[element] = std::stoi(index);
				}
				// 要素へのIndexから、実際の要素の値を取得して、頂点を構築する
				Vector4 position = positions[static_cast<std::vector<Vector4, std::allocator<Vector4>>::size_type>(elementIndieces[0]) - 1];
				Vector2 texcoord = texcoords[static_cast<std::vector<Vector2, std::allocator<Vector2>>::size_type>(elementIndieces[1]) - 1];
				Vector3 normal = normals[static_cast<std::vector<Vector3, std::allocator<Vector3>>::size_type>(elementIndieces[2]) - 1];
				position.x *= -1;
				texcoord.y = 1.0f - texcoord.y;
				normal.x *= -1;
				/*VertexData vertex = { position,texcoord,normal };
				modelData.vertices.push_back(vertex);*/
				triangle[faceVertex] = { position,texcoord,normal };
			}
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}
		else if (identifier == "mtllib")
		{
			// materialTemplateLibraryファイル名を取得
			std::string materialFilename;
			s >> materialFilename;
			// 基本的にobjファイルと同一改装にmtlは存在させるので、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}

	//4. ModelDataを返す
	return modelData;
}

// Particle生成関数
Particle MakeNewParticle(std::mt19937& randomEngine, const Vector3& translate)
{
	Particle particle;

	// 一様分布生成期を使って乱数を生成
	std::uniform_real_distribution<float> distribution(-1.0, 1.0f);
	std::uniform_real_distribution<float> distColor(0.0, 1.0f);
	std::uniform_real_distribution<float> distTime(1.0, 3.0f);

	// 位置と速度を[-1, 1]でランダムに初期化
	particle.transform.scale = { 1.0f, 1.0f, 1.0f };
	particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
	particle.transform.translate = { distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };

	// 発生場所を計算
	Vector3 randomTranslate{ distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };
	particle.transform.translate = translate + randomTranslate;

	// 色を[0, 1]でランダムに初期化
	particle.color = { distColor(randomEngine), distColor(randomEngine), distColor(randomEngine), 1.0f };

	// パーティクル生成時にランダムに1秒～3秒の間生存
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;
	particle.velocity = { distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };

	return particle;
}

// パーティクルを射出する関数
std::list<Particle> Emit(const Emitter& emitter, std::mt19937& randomEngine)
{
	std::list<Particle> particles;
	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeNewParticle(randomEngine, emitter.transform.translate));
	}

	return particles;
}

// 当たり判定
bool IsCollision(const AABB& aabb, const Vector3& point)
{
	// 点がAABBの範囲内にあるかチェック
	return (point.x >= aabb.min.x && point.x <= aabb.max.x &&
		point.y >= aabb.min.y && point.y <= aabb.max.y &&
		point.z >= aabb.min.z && point.z <= aabb.max.z);
}

// パーティクルの方向を制御する関数
Particle MakeNewParticleInArc(std::mt19937& randomEngine, const Vector3& translate, float arcRadius, float arcAngleStart, float arcAngleEnd)
{
	Particle particle{};

	// 位置とスケールを初期化
	particle.transform.scale = { 1.0f,1.0f,1.0f };
	particle.transform.rotate = { 0.0f,0.0f,0.0f };

	// 色と生存時間をランダムに設定
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
	particle.color = { distColor(randomEngine),distColor(randomEngine),distColor(randomEngine),1.0f };

	// 生存時間をランダムに設定
	std::uniform_real_distribution<float> distTime(1.0f, 5.0f);
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 弧の中でランダムな角度を生成
	std::uniform_real_distribution<float> distAngle(arcAngleStart, arcAngleEnd);
	float angle = distAngle(randomEngine);

	// この範囲に基づいて位置と速度を計算
	float radians = angle * (pi / 180.0f); // 角度をラジアンに変換
	particle.transform.translate = translate + Vector3{ 0.0f, arcRadius * cos(radians), arcRadius * sin(radians) };
	particle.velocity = { 0.0f, cos(radians), sin(radians) };

	return particle;
}

// 弧を描くパーティクル群を生成する関数
std::list<Particle> EmitInArc(const Emitter& emitter, std::mt19937& randomEngine, float arcRadius, float arcAngleStart, float arcAngleEnd)
{
	std::list<Particle> particles{};
	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeNewParticleInArc(randomEngine, emitter.transform.translate, arcRadius, arcAngleStart, arcAngleEnd));
	}

	return particles;
}

// 大砲発射エフェクト用関数
Particle MakeCannonParticle3D(std::mt19937& randomEngine, const Vector3& translate, const Vector3& forward, const Vector3& up, float spreadAngle, float minSpeed, float maxSpeed)
{
	Particle particle{};

	// スケールと回転を初期化
	particle.transform.scale = { 1.0f,1.0f,1.0f };
	particle.transform.rotate = { 0.0f,0.0f,0.0f };

	// 発射位置を設定
	particle.transform.translate = translate;

	// 色を設定
	std::uniform_real_distribution<float> distColor(0.8f, 1.0f); // 明るいオレンジや黄色
	particle.color = { distColor(randomEngine), distColor(randomEngine) * 0.5f, 0.0f, 1.0f };

	// 生成時間をランダムに設定
	std::uniform_real_distribution<float> distTime(0.2f, 1.0f); // 短い寿命
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 拡散角度をランダムに設定
	std::uniform_real_distribution<float> distAngle(-spreadAngle, spreadAngle);
	float horizontalAngle = distAngle(randomEngine);
	float verticalAngle = distAngle(randomEngine);

	// ラジアンに変換
	float hRadians = horizontalAngle * (pi / 180.0f);
	float vRadians = verticalAngle * (pi / 180.0f);

	// 速度をランダムに設定
	std::uniform_real_distribution<float> distSpeed(minSpeed, maxSpeed);
	float speed = distSpeed(randomEngine);

	// パーティクルの速度を計算（拡散範囲内で前方方向を変化させる）
	Vector3 right = Normalize(Cross(up, forward)); // 右方向ベクトル
	Vector3 direction =
		forward * cos(vRadians) * cos(hRadians) +
		right * sin(hRadians) +
		up * sin(vRadians);

	direction = Normalize(direction); // 正規化して方向ベクトルを作成

	// パーティクルの速度を設定
	particle.velocity = direction * speed;

	return particle;
}

// 大砲のパーティクル群の生成関数
std::list<Particle> EmitCannonParticles3D(const Emitter& emitter, std::mt19937& randomEngine, float spreadAngle, float minSpeed, float maxSpeed)
{
	std::list<Particle> particles{};

	// 大砲のローカル方向を計算
	Vector3 forward = TransformDirection(emitter.transform, { 0.0f, 0.0f, 1.0f }); // 前方方向
	Vector3 up = TransformDirection(emitter.transform, { 0.0f, 1.0f, 0.0f });      // 上方向

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeCannonParticle3D(randomEngine, emitter.transform.translate, forward, up, spreadAngle, minSpeed, maxSpeed));
	}

	return particles;
}

// 斬撃用のパーティクルを生成する関数
Particle MakeSlashParticle(std::mt19937& randomEngine, const Vector3& start, const Vector3& end, float spread)
{
	Particle particle{};

	// ランダム分布
	std::uniform_real_distribution<float> distColor(0.8f, 1.0f); // 明るい赤やオレンジ
	std::uniform_real_distribution<float> distSpread(-spread, spread);
	std::uniform_real_distribution<float> distTime(0.5f, 2.0f); // 寿命を短めに
	std::uniform_real_distribution<float> distScale(0.5f, 1.5f); // サイズのばらつき
	std::uniform_real_distribution<float> distLerp(0.0f, 1.0f); // 開始点と終了店の間の位置を計算

	// 軌跡方向ベクトルを計算 (斬撃の開始点と終了点を結ぶ方向)
	Vector3 direction = Normalize(end - start);

	// 生成位置を設定（開始位置と終了位置の間をランダムに決定）
	float lerpFactor = distLerp(randomEngine);
	particle.transform.translate = start + (end - start) * lerpFactor;

	// 軌跡にばらつきを追加
	Vector3 spreadOffset = {
		distSpread(randomEngine),  // X軸方向
		distSpread(randomEngine),  // Y軸方向
		distSpread(randomEngine)   // Z軸方向
	};
	particle.transform.translate += spreadOffset;

	// 色を設定
	particle.color = { distColor(randomEngine), distColor(randomEngine) * 0.2f,0.0f,1.0f };

	// パーティクルの大きさと寿命をランダムに設定
	particle.transform.scale = { distScale(randomEngine), distScale(randomEngine), distScale(randomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 軌跡方向に速度を設定
	particle.velocity = direction * distScale(randomEngine);

	return particle;
}

// 斬撃パーティクルを射出する関数
std::list<Particle> EmitSlashParticles(const Emitter& emitter, std::mt19937& randomEngine, const Vector3& end, float spread)
{
	std::list<Particle> particles{};

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeSlashParticle(randomEngine, emitter.transform.translate, end, spread));
	}
	return particles;
}

// 炎パーティクル生成関数
Particle MakeFlameParticle(std::mt19937& randomEngine, const Vector3& position, float spread)
{
	Particle particle{};

	// ランダム分布
	std::uniform_real_distribution<float> distColor(0.7f, 1.0f); // 明るい赤～オレンジ
	std::uniform_real_distribution<float> distSpread(-spread, spread);
	std::uniform_real_distribution<float> distTime(1.0f, 3.0f); // 炎の寿命
	std::uniform_real_distribution<float> distScale(0.5f, 2.0f); // 炎の大きさ

	// 初期位置
	particle.transform.translate = position + Vector3{
		distSpread(randomEngine),
		distSpread(randomEngine),
		distSpread(randomEngine) };

	// 色
	particle.color = { distColor(randomEngine), distColor(randomEngine) * 0.5f, 0.0f, 1.0f };

	// 大きさと寿命
	particle.transform.scale = { distScale(randomEngine), distScale(randomEngine), distScale(randomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 上昇する速度
	particle.velocity = { 0.0f, distScale(randomEngine) * 2.0f, 0.0f };

	return particle;
}

// 炎パーティクル射出関数
std::list<Particle> EmitFlameParticles(const Emitter& emitter, std::mt19937& randomEngine, const Vector3& position, float spread)
{
	std::list<Particle> particles{};

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeFlameParticle(randomEngine, position, spread));
	}
	return particles;
}

// 水パーティクル生成関数
Particle MakeWaterParticle(std::mt19937& randomEngine, const Vector3& position, float spread)
{
	Particle particle{};

	// ランダム分布
	std::uniform_real_distribution<float> distColor(0.3f, 0.6f);  // 青色の範囲
	std::uniform_real_distribution<float> distSpread(-spread, spread);
	std::uniform_real_distribution<float> distTime(1.5f, 4.0f);   // 水の寿命
	std::uniform_real_distribution<float> distScale(0.3f, 1.2f);  // サイズのばらつき
	std::uniform_real_distribution<float> distVelocity(-0.5f, 0.5f); // 水の流れる方向のばらつき

	// 初期位置
	particle.transform.translate = position + Vector3{
		distSpread(randomEngine),
		distSpread(randomEngine),
		distSpread(randomEngine) };

	// 色（青系で透明感のある色）
	float blue = distColor(randomEngine);
	particle.color = { 0.0f, 0.3f * blue, blue, 0.8f };

	// 大きさと寿命
	particle.transform.scale = { distScale(randomEngine), distScale(randomEngine), distScale(randomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 水の流れるような速度
	particle.velocity = {
		distVelocity(randomEngine),               // X軸のランダム速度
		-0.2f * distScale(randomEngine),          // Y軸の落下速度
		distVelocity(randomEngine) };             // Z軸のランダム速度

	return particle;
}

// 水パーティクル射出関数
std::list<Particle> EmitWaterParticles(const Emitter& emitter, std::mt19937& randomEngine, const Vector3& position, float spread)
{
	std::list<Particle> particles{};

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeWaterParticle(randomEngine, position, spread));
	}
	return particles;
}

// 雷パーティクル生成関数
Particle MakeLightningParticle(std::mt19937& randomEngine, const Vector3& start, const Vector3& end, float spread)
{
	Particle particle{};

	// ランダム分布
	std::uniform_real_distribution<float> distColor(0.8f, 1.0f);   // 明るい青や白
	std::uniform_real_distribution<float> distSpread(-spread, spread);
	std::uniform_real_distribution<float> distTime(0.2f, 0.8f);    // 雷の寿命は短め
	std::uniform_real_distribution<float> distScale(0.1f, 0.5f);   // 細く鋭い線を表現
	std::uniform_real_distribution<float> distLerp(0.0f, 1.0f);    // 開始点と終了点間のランダム位置

	// 軌跡方向ベクトルを計算
	Vector3 direction = Normalize(end - start);

	// 初期位置（開始点と終了点の間のランダムな位置）
	float lerpFactor = distLerp(randomEngine);
	particle.transform.translate = start + (end - start) * lerpFactor;

	// 軌跡にジグザグのばらつきを追加
	Vector3 spreadOffset = {
		distSpread(randomEngine),
		distSpread(randomEngine),
		distSpread(randomEngine) };
	particle.transform.translate += spreadOffset;

	// 色（青白く光る雷をイメージ）
	particle.color = { distColor(randomEngine), distColor(randomEngine), 1.0f, 1.0f };

	// 大きさと寿命
	particle.transform.scale = { distScale(randomEngine), distScale(randomEngine), distScale(randomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 軌跡方向に高速で移動する
	particle.velocity = direction * 20.0f; // 高速移動

	return particle;
}

// 雷パーティクル射出関数
std::list<Particle> EmitLightningParticles(const Emitter& emitter, std::mt19937& randomEngine, const Vector3& end, float spread)
{
	std::list<Particle> particles{};

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeLightningParticle(randomEngine, emitter.transform.translate, end, spread));
	}
	return particles;
}

// 風パーティクル生成関数
Particle MakeWindParticle(std::mt19937& randomEngine, const Vector3& position, float spread)
{
	Particle particle{};

	// ランダム分布
	std::uniform_real_distribution<float> distColor(0.5f, 0.8f);   // 柔らかい青や白
	std::uniform_real_distribution<float> distSpread(-spread, spread);
	std::uniform_real_distribution<float> distTime(2.0f, 5.0f);    // 風の寿命は長め
	std::uniform_real_distribution<float> distScale(0.3f, 1.0f);   // 風の柔らかな広がりを表現
	std::uniform_real_distribution<float> distVelocity(0.2f, 1.0f); // 一方向への緩やかな動き

	// 初期位置（中心からランダムに広がる）
	particle.transform.translate = position + Vector3{
		distSpread(randomEngine),
		distSpread(randomEngine) * 0.5f, // 垂直方向は少し抑える
		distSpread(randomEngine) };

	// 色（透明感のある白や淡い青）
	float brightness = distColor(randomEngine);
	particle.color = { 0.8f * brightness, 0.9f * brightness, brightness, 0.5f }; // 半透明

	// 大きさと寿命
	particle.transform.scale = { distScale(randomEngine), distScale(randomEngine), distScale(randomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTIme = 0;

	// 柔らかい一方向の流れを表現する速度
	particle.velocity = {
		distVelocity(randomEngine),               // X軸方向のランダムな速度
		distVelocity(randomEngine) * 0.2f,        // 垂直方向の動きは弱め
		distVelocity(randomEngine) * 0.5f };       // Z軸方向の緩やかな流れ

	return particle;
}

// 風パーティクル射出関数
std::list<Particle> EmitWindParticles(const Emitter& emitter, std::mt19937& randomEngine, const Vector3& position, float spread)
{
	std::list<Particle> particles{};

	for (uint32_t count = 0; count < emitter.count; ++count)
	{
		particles.push_back(MakeWindParticle(randomEngine, position, spread));
	}
	return particles;
}


// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	//COMの初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);

	D3DResourceLeakChecker leakCheck;

#pragma region Window
	// ウィンドウクラスを登録する
	WNDCLASS wc{};
	//ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	//ウィンドウクラス名（なんでもいい）
	wc.lpszClassName = L"CG2WindowClass";
	//インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	//ウィンドウクラスを登録する
	RegisterClass(&wc);

	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	//クライアント領域をmとに実際のサイズにwrcに変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,				//利用するクラス名
		L"CG2",							//タイトルバーの文字
		WS_OVERLAPPEDWINDOW,			//よく見るウィンドウスタイル
		CW_USEDEFAULT,					//表示X座標
		CW_USEDEFAULT,					//表示Y座標
		wrc.right - wrc.left,			//ウィンドウ横幅
		wrc.bottom - wrc.top,			//ウィンドウ縦幅
		nullptr,						//親ウィンドウハンドル
		nullptr,						//メニューハンドル
		wc.hInstance,					//インスタンスハンドル
		nullptr);						//オプション

	//ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);
	MSG msg{};
#pragma endregion


	// DebugLayer(デバッグレイヤー)
#ifdef _DEBUG
	Microsoft::WRL::ComPtr <ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		//デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		//さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif


#pragma region IDXGIFactory
	//DXGIファクトリーの生成
	Microsoft::WRL::ComPtr <IDXGIFactory7> dxgiFactory = nullptr;
	//HRESULTはWindows系のエラーコードであり、
	//関数が成功したかどうかをSUCCEEDEDマクロ判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	//初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合があるのでassertにしておく
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma	region Adaptor
	//使用するアダプタ用の変数。最初にnullptrを入れておく
	Microsoft::WRL::ComPtr <IDXGIAdapter4> useAdapter = nullptr;
	//良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND, ++i;)
	{
		//アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));	//取得できないのは一大事
		//ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			//採用したアダプタの情報をログに出力。wstringの法なので注意
			Log(ConvertString(std::format(L"Use Adapater\n:{}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr;	//ソフトウェアアダプタの場合は見なかったことにする
	}
	//適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);
#pragma endregion


#pragma region D3D12Device
	// D3D12Deviceの生成
	Microsoft::WRL::ComPtr <ID3D12Device> device = nullptr;
	//機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0 };
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	//高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i)
	{
		//採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		//指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr))
		{
			//生成できたのでログ出力を行ってループを抜ける
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	//デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);
	Log("Complete create D3D12Device!!!\n");	//初期化完了のログを出す
#pragma endregion


	// エラー・警告、すなわち停止
#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
	{
		//ヤバいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		//エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		/*全部の情報を出す*/

		//警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		//抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] =
		{
			//Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		//抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		//指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
		//解放
		infoQueue->Release();
	}
#endif // _DEBUG


#pragma region commandQueue
	//コマンドキューを生成する
	Microsoft::WRL::ComPtr <ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	//コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region commandList
	//コマンドロケータを生成する
	Microsoft::WRL::ComPtr <ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	//コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドリストを生成する
	Microsoft::WRL::ComPtr <ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	//コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region SwapChain
	//スワップチェーンを生成する
	Microsoft::WRL::ComPtr <IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;								//画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight;							//画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;				//色の形式
	swapChainDesc.SampleDesc.Count = 1;								//マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	//描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;									//ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;		//モニタにうつしたら、中身を破壊
	//コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region DescriptorHeap
	//RTVディスクイリプタヒープの生成
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	//SRVディスクイリプタヒープの生成
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	//DSV用のヒープでディスクリプタの数は１。DSVはShader内で触れるものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
#pragma endregion


#pragma region DescriptorSize
	//DescriptorSizeを取得しておく
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
#pragma endregion


#pragma region SwapChainからResourceを取ってくる
	//SwapChainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr <ID3D12Resource> swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	//うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region RTV
	//RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;		//出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;	//2dテクスチャとして書き込む
	//ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 0);
	//RTVを2つ作るのでディスクリプタを２つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	//まず１つ目を作る。１つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	//2つ目のディスクリプタハンドルを得る（自力で）
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//2つ目を作る
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);
#pragma endregion


#pragma region DSV
	//DepthStencilTextureをウィンドウのサイズで作成
	Microsoft::WRL::ComPtr <ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight);
	//DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;			//Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;	//2DTexture
	//DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0));
#pragma endregion


#pragma region Fence&Event
	// FenceとEventを生成する
	Microsoft::WRL::ComPtr <ID3D12Fence> fence = nullptr;
	//初期値0でFenceを作る
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));
	//FenceのSignalを待つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
#pragma endregion


#pragma region DXC_Initialization
	//dxcCompilerを初期化
	Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	//現時点でincludeはしないが、includeに対応するための設定を行っておく
	Microsoft::WRL::ComPtr <IDxcIncludeHandler> includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region PSO(Pipeline State Object)
	//RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#pragma endregion

	// Particle用のRootSignature
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0; // 0から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;		// 数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;								// CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;								// PixelShaderを使う
	rootParameters[0].Descriptor.ShaderRegister = 0;												// レジスタ番号０とバインド

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;					// DescriptorTableを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;							// VertexShaderで使う
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;				// Tableの中身の配列を指定
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;					// DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;								// PixelxShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;				// Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する

	descriptionRootSignature.pParameters = rootParameters;											//ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);								//配列の長さ

#pragma region Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;										//バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;									//0～1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;									//比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;													//ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;															//レジスタ番号０を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;								//PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);
#pragma endregion


#pragma region シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr <ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr <ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr))
	{
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	//バイナリを元に生成
	Microsoft::WRL::ComPtr <ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region InputLayoutの設定を行う・InputLayoutの拡張
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);
#pragma endregion


#pragma region BlendStateの設定を行う
	//BlendStateの設定
	D3D12_RENDER_TARGET_BLEND_DESC blendDesc{};

	// ブレンドするかしないか
	blendDesc.BlendEnable = false;
	// すべての色要素を書き込む
	blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 各ブレンドモードの設定を行う
	switch (currentBlendMode)
	{
		// ブレンドモードなし
	case BlendMode::kBlendModeNone:

		blendDesc.BlendEnable = false;
		break;

		// 通常αブレンドモード
	case BlendMode::kBlendModeNormal:

		// ノーマル
		blendDesc.BlendEnable = true;
		blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
		break;

		// 加算ブレンドモード
	case BlendMode::kBlendModeAdd:

		// 加算
		blendDesc.BlendEnable = true;
		blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.DestBlend = D3D12_BLEND_ONE;
		blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		  // アルファのソースはそのまま
		blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	  // アルファの加算操作
		blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	  // アルファのデスティネーションは無視
		break;

		// 減算ブレンドモード
	case BlendMode::kBlendModeSubtract:

		// 減算
		blendDesc.BlendEnable = true;
		blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		blendDesc.DestBlend = D3D12_BLEND_ONE;
		blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
		blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
		blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無
		break;

		// 乗算ブレンドモード
	case BlendMode::kBlendModeMultiply:

		// 乗算
		blendDesc.BlendEnable = true;
		blendDesc.SrcBlend = D3D12_BLEND_ZERO;
		blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.DestBlend = D3D12_BLEND_SRC_COLOR;
		blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
		blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
		blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無視
		break;

		// スクリーンブレンドモード
	case BlendMode::kBlendModeScreen:

		// スクリーン
		blendDesc.BlendEnable = true;
		blendDesc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.DestBlend = D3D12_BLEND_ONE;
		blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
		blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
		blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無視
		break;

		// 無効なブレンドモード
	default:
		// 無効なモードの処理
		assert(false && "Invalid Blend Mode");
		break;
	}

#pragma endregion


#pragma region RasterizerStateの設定を行う
	//RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	//裏面（時計回り）を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	//三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
#pragma endregion


#pragma region ShaderをCompileする
	//Shaderをコンパイルする
	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = CompilerShader(L"Particle.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler, includeHandler.Get());
	assert(vertexShaderBlob != nullptr);

	//Pixelをコンパイルする
	Microsoft::WRL::ComPtr <IDxcBlob> pixelShaderBlob = CompilerShader(L"Particle.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler, includeHandler.Get());
	assert(pixelShaderBlob != nullptr);
#pragma endregion


#pragma region DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	//Depthを描くのをやめる
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
#pragma endregion


#pragma region グラフィックスパイプラインステートオブジェクト（Pipeline State Object, PSO）を生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};												// パイプラインステートディスクリプタの初期化
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();												// RootSgnature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;													// InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),vertexShaderBlob->GetBufferSize() };	// VertexDhader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),pixelShaderBlob->GetBufferSize() };	// PixelShader
	graphicsPipelineStateDesc.BlendState.RenderTarget[0] = blendDesc;															// BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;													// RasterizeerState

	//レンダーターゲットの設定
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	//利用するトポロジー（形態）のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;					// プリミティブトポロジーの設定

	// サンプルマスクとサンプル記述子の設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// DepthStencilステートの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// パイプラインステートオブジェクトの生成
	Microsoft::WRL::ComPtr <ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));
#pragma endregion


#pragma region マテリアル用のリソースを作成しそのリソースにデータを書き込む処理を行う
	//マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr <ID3D12Resource> materialResource = CreateBufferResource(device.Get(), sizeof(Material));
	//マテリアルにデータを書き込む
	Material* materialData = nullptr;
	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	//今回は赤を書き込んでみる
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = true;
	//UVTramsform行列を単位行列で初期化
	materialData->uvTransform = MakeIdentity();
#pragma endregion


#pragma region スプライト用のマテリアルリソースを作成し設定する処理を行う
	//スプライト用のマテリアルソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> materialResourceSprite = CreateBufferResource(device.Get(), sizeof(Material));
	Material* materialDataSprite = nullptr;
	//書き込むためのアドレスを取得
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	//SpriteはLightingしないのでfalseを設定する
	materialDataSprite->enableLighting = false;
	////UVTramsform行列を単位行列で初期化(スプライト用)
	materialDataSprite->uvTransform = MakeIdentity();
#pragma endregion


#pragma region 平行光源のプロパティ 色 方向 強度 を格納するバッファリソースを生成しその初期値を設定
	//平行光源用のリソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	//書き込むためのアドレスを取得
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

	directionalLightData->color = { 1.0f,1.0f,1.0f ,1.0f };
	directionalLightData->direction = { 0.0f, 1.0f,0.0f };
	directionalLightData->intensity = 1.0f;
#pragma endregion


#pragma region WVP行列データを格納するバッファリソースを生成し初期値として単位行列を設定
	//WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr <ID3D12Resource> wvpResource = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* wvpData = nullptr;
	//書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	//単位行列を書き込んでおく
	wvpData->World = MakeIdentity();
	wvpData->WVP = MakeIdentity();
#pragma endregion

	// Particle用のResource作成

	// 描画数
	const uint32_t kNumMaxInstance = 100;

	// Instancing用のTransformationMatrixを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = CreateBufferResource(device.Get(), sizeof(ParticleForGPU) * kNumMaxInstance);
	// 書き込むためのアドレスを取得
	ParticleForGPU* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));
	// 単位行列を書き込んでおく
	for (uint32_t index = 0; index < kNumMaxInstance; ++index)
	{
		instancingData[index].WVP = MakeIdentity();
		instancingData[index].World = MakeIdentity();
	}


#pragma region スプライトの頂点バッファリソースと変換行列リソースを生成
	//Sprite用の頂点リソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResourceSprite = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);

	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点データを設定する
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	//1枚目の三角形
	vertexDataSprite[0].position = { 0.0f,360.0f,0.0f,1.0f };		//左下
	vertexDataSprite[0].texcoord = { 0.0f,1.0f };
	vertexDataSprite[1].position = { 0.0f,0.0f,0.0f,1.0f };			//左上
	vertexDataSprite[1].texcoord = { 0.0f,0.0f };
	vertexDataSprite[2].position = { 640.0f,360.0f,0.0f,1.0f };		//右下
	vertexDataSprite[2].texcoord = { 1.0f,1.0f };

	//2枚目の三角形
	vertexDataSprite[3].position = { 0.0f,0.0f,0.0f,1.0f };			//左上
	vertexDataSprite[3].texcoord = { 0.0f,0.0f };
	vertexDataSprite[4].position = { 640.0f,0.0f,0.0f,1.0f };		//右上
	vertexDataSprite[4].texcoord = { 1.0f,0.0f };
	vertexDataSprite[5].position = { 640.0f,360.0f,0.0f,1.0f };		//右下
	vertexDataSprite[5].texcoord = { 1.0f,1.0f };

	// 法線情報を追加する
	for (int i = 0; i < 6; ++i) {
		vertexDataSprite[i].normal = { 0.0f, 0.0f, -1.0f };
	}

	//Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr <ID3D12Resource> transfomationMatrixResourceSprite = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));

	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transfomationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	//単位行列を書き込んでおく
	transformationMatrixDataSprite->World = MakeIdentity();
	transformationMatrixDataSprite->WVP = MakeIdentity();
#pragma endregion


#pragma region スプライトのインデックスバッファを作成および設定する
	Microsoft::WRL::ComPtr <ID3D12Resource> indexResourceSprite = CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	//リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	//使用するリソースのサイズはインデックス６つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	//インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 4; indexDataSprite[5] = 2;
#pragma endregion


#pragma region テクスチャファイルを読み込みテクスチャリソースを作成しそれに対してSRVを設定してこれらをデスクリプタヒープにバインド
	// モデルの読み込み
	ModelData modelData;// = LoadObjFile("resources", "plane.obj");

	// 6つの頂点を定義して四角形を表現
	modelData.vertices.push_back({ .position = {-1.0f, 1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} });  // 左上
	modelData.vertices.push_back({ .position = {1.0f, 1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} }); // 右上
	modelData.vertices.push_back({ .position = {-1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} }); // 左下

	modelData.vertices.push_back({ .position = {-1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} }); // 左下
	modelData.vertices.push_back({ .position = {1.0f, 1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} }); // 右上
	modelData.vertices.push_back({ .position = {1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} }); // 右下

	// テクスチャの設定
	modelData.material.textureFilePath = "./resources/particle.png";

	//Textureを読んで転送する
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr <ID3D12Resource> textureResource = CreateTextureResource(device.Get(), metadata);
	UploadTextureData(textureResource.Get(), mipImages);

	// 1つ目のテクスチャのSRV設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;				//2Dテクスチャ
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// 1つ目のテクスチャのSRVのデスクリプタヒープへのバインド
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

	//2枚目のTextureを読んで転送する
	DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr <ID3D12Resource> textureResource2 = CreateTextureResource(device.Get(), metadata2);
	UploadTextureData(textureResource2.Get(), mipImages2);

	// 2つ目のテクスチャのSRV設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;				//2Dテクスチャ
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// 2つ目のテクスチャのSRVのデスクリプタヒープへのバインド
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
	device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);
#pragma endregion

	// Particle用のSRVの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
	device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

#pragma region 球体の頂点データを格納するためのバッファリソースを生成
	// 分割数
	uint32_t kSubdivision = 20;
	// 緯度・経度の分割数に応じた角度の計算
	float kLatEvery = pi / float(kSubdivision);
	float kLonEvery = 2.0f * pi / float(kSubdivision);
	// 球体の頂点数の計算
	uint32_t TotalVertexCount = kSubdivision * kSubdivision * 6;

	// バッファリソースの作成
	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * (modelData.vertices.size() + TotalVertexCount));
#pragma endregion


#pragma region 頂点バッファデータの開始位置サイズおよび各頂点のデータ構造を指定
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};																 // 頂点バッファビューを作成する
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();									 // リソースの先頭のアドレスから使う
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * (modelData.vertices.size() + TotalVertexCount));	 // 使用するリソースのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);														 // 1頂点あたりのサイズ
#pragma endregion


#pragma region 球体の頂点位置テクスチャ座標および法線ベクトルを計算し頂点バッファに書き込む
	VertexData* vertexData = nullptr;																			 // 頂点リソースにデータを書き込む
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));										 // 書き込むためのアドレスを取得

	// モデルデータの頂点データをコピー
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// 球体の頂点データをコピー
	VertexData* sphereVertexData = vertexData + modelData.vertices.size();
	auto calculateVertex = [](float lat, float lon, float u, float v) {
		VertexData vertex;
		vertex.position = { cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon), 1.0f };
		vertex.texcoord = { u, v };
		vertex.normal = { vertex.position.x, vertex.position.y, vertex.position.z };
		return vertex;
		};

	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -pi / 2.0f + kLatEvery * latIndex; // θ
		float nextLat = lat + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float u = float(lonIndex) / float(kSubdivision);
			float v = 1.0f - float(latIndex) / float(kSubdivision);
			float lon = lonIndex * kLonEvery; // Φ
			float nextLon = lon + kLonEvery;

			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

			// 6つの頂点を計算
			sphereVertexData[start + 0] = calculateVertex(lat, lon, u, v);
			sphereVertexData[start + 1] = calculateVertex(nextLat, lon, u, v - 1.0f / float(kSubdivision));
			sphereVertexData[start + 2] = calculateVertex(lat, nextLon, u + 1.0f / float(kSubdivision), v);
			sphereVertexData[start + 3] = calculateVertex(nextLat, nextLon, u + 1.0f / float(kSubdivision), v - 1.0f / float(kSubdivision));
			sphereVertexData[start + 4] = calculateVertex(lat, nextLon, u + 1.0f / float(kSubdivision), v);
			sphereVertexData[start + 5] = calculateVertex(nextLat, lon, u, v - 1.0f / float(kSubdivision));
		}
	}

	// アンマップ
	vertexResource->Unmap(0, nullptr);
#pragma endregion


#pragma region 描画パイプラインで使用するビューポートとシザー矩形を設定
	//ビューポート
	D3D12_VIEWPORT viewport{};
	//クライアント領域のサイズと一緒に画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	//シザー矩形
	D3D12_RECT scissorRect{};
	//基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;
#pragma endregion


#pragma region ImGuiの初期化を行いDirectX 12とWindows APIを使ってImGuiをセットアップする
	IMGUI_CHECKVERSION();			// ImGuiのバージョンチェック
	ImGui::CreateContext();			// ImGuiコンテキストの作成
	ImGui::StyleColorsDark();		// ImGuiスタイルの設定
	ImGui_ImplWin32_Init(hwnd);		// Win32バックエンドの初期化
	ImGui_ImplDX12_Init(device.Get(),		// DirectX 12バックエンドの初期化
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
#pragma endregion


	bool isMove = true;
	bool useBillboard = true;
	bool isWind = false;
	bool isParticle = false;

	// 乱数生成期の初期化
	std::random_device seedGenerator;
	std::mt19937 randomEngine(seedGenerator());

	// Δt を定義。とりあえず60fps固定してあるが、実時間を計測して可変fpsで動かせるようにする
	const float kDeltaTime = 1.0f / 60.0f;

	uint32_t numInstance = 0; // 描画すべきインスタンス数

	// エミッター
	Emitter emitter{};
	emitter.count = 3;
	emitter.frequency = 0.5f; // 0.5秒ごとに発生
	emitter.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	emitter.transform = {
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 0.0f}
	};

	// エミッター
	Emitter emitter2{};
	emitter2.count = 7;
	emitter2.frequency = 0.5f; // 0.5秒ごとに発生
	emitter2.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter3{};
	emitter3.count = 10;
	emitter3.frequency = 0.5f; // 0.5秒ごとに発生
	emitter3.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter4{};
	emitter4.count = 20;
	emitter4.frequency = 0.5f; // 0.5秒ごとに発生
	emitter4.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter5{};
	emitter5.count = 50;
	emitter5.frequency = 0.5f; // 0.5秒ごとに発生
	emitter5.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter6{};
	emitter6.count = 25;
	emitter6.frequency = 0.5f; // 0.5秒ごとに発生
	emitter6.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter7{};
	emitter7.count = 25;
	emitter7.frequency = 0.5f; // 0.5秒ごとに発生
	emitter7.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// エミッター
	Emitter emitter8{};
	emitter8.count = 25;
	emitter8.frequency = 0.5f; // 0.5秒ごとに発生
	emitter8.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	Emitter emitter9{};
	emitter9.count = 5;
	emitter9.frequency = 0.5f; // 0.5秒ごとに発生
	emitter9.frequencyTime = 0.0f; // 発生時刻用の時刻、0で初期化

	// パーティクルをリストで管理する
	std::list<Particle> particles;

	//Tramsform変数を作る
	Transform transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	Transform cameraTransform = { {1.0f, 1.0f, 1.0f}, {-0.5f, 1.3f, 0.0f}, {-63.0f, -36.0f, -20.0f} };

	Transform transformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	//UVTransform用の変数を用意
	Transform uvTransformSprite{ {1.0f,1.0f,1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f}, };

	// Y軸でπ/2回転させる
	Matrix4x4 backToFrontMatrix = MakeIdentity();

	Matrix4x4 scaleMatrix{};
	scaleMatrix.m[3][0] = 0.0f;
	scaleMatrix.m[3][1] = 0.0f;
	scaleMatrix.m[3][2] = 0.0f;

	Matrix4x4 translateMatrix{};
	translateMatrix.m[3][0] = 0.0f;
	translateMatrix.m[3][1] = 0.0f;
	translateMatrix.m[3][2] = 0.0f;

	// Fieldを作る
	AccelerationField accelerationField;
	accelerationField.acceleration = { 15.0f, 0.0f, 0.0f };
	accelerationField.area.min = { -10.0f, -10.0f, -10.0f };
	accelerationField.area.max = { 10.0f, 10.0f, 10.0f };

	std::vector<WindZone> windZones = {
	{ { {-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f} }, {0.1f, 0.0f, 0.0f} },
	{ { {10.0f, -5.0f, -5.0f}, {15.0f, 5.0f, 5.0f} }, {0.0f, 0.0f, 0.1f} }
	};

	//ウィンドウのｘボタンが押されるまでループ
	while (msg.message != WM_QUIT)
	{
		//Windowにメッセージが来たら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			//ImGuiを使う
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();


			// ImGuiウィジェットの作成（描画ループ内）
			ImGui::Begin("Settings");

			ImGui::ColorEdit4("color", &materialData->color.x);

			if (ImGui::Combo("Blend Mode", reinterpret_cast<int*>(&currentBlendMode), blendModeNames, kcountOfBlendMode)) {
				// ブレンドモードが変更されたときに、パイプラインステートを再構築する処理を追加
				//BlendStateの設定
				D3D12_RENDER_TARGET_BLEND_DESC blendDesc{};

				// ブレンドするかしないか
				blendDesc.BlendEnable = false;
				// すべての色要素を書き込む
				blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

				// 各ブレンドモードの設定を行う
				switch (currentBlendMode)
				{
					// ブレンドモードなし
				case BlendMode::kBlendModeNone:

					blendDesc.BlendEnable = false;
					break;

					// 通常αブレンドモード
				case BlendMode::kBlendModeNormal:

					// ノーマル
					blendDesc.BlendEnable = true;
					blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
					blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
					blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
					blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
					blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
					blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
					break;

					// 加算ブレンドモード
				case BlendMode::kBlendModeAdd:

					// 加算
					blendDesc.BlendEnable = true;
					blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
					blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
					blendDesc.DestBlend = D3D12_BLEND_ONE;
					blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		  // アルファのソースはそのまま
					blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	  // アルファの加算操作
					blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	  // アルファのデスティネーションは無視
					break;

					// 減算ブレンドモード
				case BlendMode::kBlendModeSubtract:

					// 減算
					blendDesc.BlendEnable = true;
					blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
					blendDesc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
					blendDesc.DestBlend = D3D12_BLEND_ONE;
					blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
					blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
					blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無
					break;

					// 乗算ブレンドモード
				case BlendMode::kBlendModeMultiply:

					// 乗算
					blendDesc.BlendEnable = true;
					blendDesc.SrcBlend = D3D12_BLEND_ZERO;
					blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
					blendDesc.DestBlend = D3D12_BLEND_SRC_COLOR;
					blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
					blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
					blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無視
					break;

					// スクリーンブレンドモード
				case BlendMode::kBlendModeScreen:

					// スクリーン
					blendDesc.BlendEnable = true;
					blendDesc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
					blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
					blendDesc.DestBlend = D3D12_BLEND_ONE;
					blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		 // アルファのソースはそのまま
					blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	 // アルファの加算操作
					blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	 // アルファのデスティネーションは無視
					break;

					// 無効なブレンドモード
				default:
					// 無効なモードの処理
					assert(false && "Invalid Blend Mode");
					break;
				}

				// グラフィックスパイプラインステートを更新
				graphicsPipelineStateDesc.BlendState.RenderTarget[0] = blendDesc;
				device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
			}
			ImGui::End();

			//開発用のUIの処理。実際に開発用のUIを出す場合はここをゲーム固有の処理に置き換える
			ImGui::ShowDemoWindow();

			// Gui
			{
				ImGui::Begin("Test Window");
				ImGui::DragFloat3("cameraTranslate", &cameraTransform.translate.x, 0.01f);
				ImGui::SliderAngle("CameraRotateX", &cameraTransform.rotate.x);
				ImGui::SliderAngle("CameraRotateY", &cameraTransform.rotate.y);
				ImGui::SliderAngle("CameraRotateZ", &cameraTransform.rotate.z);

				ImGui::Checkbox("Particle", &isParticle);
				ImGui::Checkbox("Move", &isMove);
				ImGui::Checkbox("useBillboard", &useBillboard);
				ImGui::Checkbox("Wind", &isWind);


				if (ImGui::Button("Add Particle"))
				{
					particles.splice(particles.end(), Emit(emitter, randomEngine));
				}

				if (ImGui::Button("Emit Arc Particles"))
				{
					float arcRadius = 5.0f;             // 弧の半径
					float arcAngleStart = -45.0f;       // 弧の開始角度（度単位）
					float arcAngleEnd = 45.0f;          // 弧の終了角度（度単位）

					particles.splice(particles.end(), EmitInArc(emitter2, randomEngine, arcRadius, arcAngleStart, arcAngleEnd));
				}

				if (ImGui::Button("Fire Cannon"))
				{
					float spreadAngle = 15.0f; // 拡散角度 (度)
					float minSpeed = 100.0f;   // 最小速度
					float maxSpeed = 200.0f;   // 最大速度

					particles.splice(particles.end(), EmitCannonParticles3D(emitter3, randomEngine, spreadAngle, minSpeed, maxSpeed));
				}

				if (ImGui::Button("Slash Attack"))
				{
					float spread = 0.1f; // パーティクルの散らばり具合
					int steps = 20;      // 円弧の分割数
					float radius = 5.0f; // 弧の半径
					float startAngle = -60.0f; // 弧の開始角度（度）
					float endAngle = 60.0f;    // 弧の終了角度（度）

					Vector3 center = emitter4.transform.translate; // 弧の中心点
					float heightOffset = 3.0f;                     // 弧全体の高さを制御

					// 弧上の点を計算しながらパーティクルを生成
					for (int i = 0; i <= steps; ++i)
					{
						float t = static_cast<float>(i) / static_cast<float>(steps);
						float angle = startAngle + t * (endAngle - startAngle); // 角度を補間
						float radians = angle * (pi / 180.0f);                 // 度をラジアンに変換

						// 弧上の位置を計算 (円周上の点 + 高さ)
						Vector3 position = {
							center.x + radius * cos(radians),  // X座標
							center.y + heightOffset * (1.0f - t), // 高さを徐々に下げる
							center.z + radius * sin(radians)   // Z座標
						};

						// 補間点をエミッター位置として渡してパーティクルを生成
						Emitter tempEmitter = emitter4;                  // 元のエミッターをコピー
						tempEmitter.transform.translate = position;      // 弧上の位置に設定
						particles.splice(particles.end(), EmitSlashParticles(tempEmitter, randomEngine, position, spread));
					}
				}

				if (ImGui::Button("Slash S Attack"))
				{
					float spread = 0.1f;     // パーティクルの散らばり具合
					int steps = 30;          // 分割数（細かいほど滑らか）
					float radius = 5.0f;     // 弧の半径
					float startAngle = -60.0f; // 弧の開始角度
					float endAngle = 60.0f;    // 弧の終了角度
					float waveAmplitude = 2.0f; // S字の振幅
					float waveFrequency = 3.0f; // S字の波の周波数

					Vector3 center = emitter4.transform.translate; // エミッターの中心位置
					float heightOffset = 3.0f; // Y軸方向の高さ変化

					for (int i = 0; i <= steps; ++i)
					{
						// t は進行度 (0.0 ～ 1.0)
						float t = static_cast<float>(i) / static_cast<float>(steps);

						// 弧を描くための角度を補間
						float angle = startAngle + t * (endAngle - startAngle);
						float radians = angle * (pi / 180.0f);

						// 基本の弧の計算 (X, Y)
						float x = center.x + radius * cos(radians);
						float y = center.y + heightOffset * (1.0f - t);

						// Z軸方向にS字カーブを追加
						float z = center.z + radius * sin(radians) + waveAmplitude * sin(waveFrequency * t * pi);

						// 最終的な位置
						Vector3 position = { x, y, z };

						// 一時的なエミッターでパーティクルを生成
						Emitter tempEmitter = emitter4;
						tempEmitter.transform.translate = position; // 計算した位置に設定
						particles.splice(particles.end(), EmitSlashParticles(tempEmitter, randomEngine, position, spread));
					}
				}

				if (ImGui::Button("Water Breathing Slash"))
				{
					int steps = 100;                // カーブの細かさ
					float radius = 5.0f;            // カーブの半径
					float height = 10.0f;           // 螺旋の高さ
					float rotations = 3.0f;         // 螺旋の回転数
					Vector3 center = emitter5.transform.translate; // エミッターの中心位置

					// ベジエ曲線の制御点を設定
					for (int i = 0; i < steps; ++i)
					{
						float t = static_cast<float>(i) / static_cast<float>(steps - 1);

						// ベジエ曲線の制御点
						Vector3 P0 = center;
						Vector3 P3 = center + Vector3{ 0.0f, height, 0.0f }; // 最終点
						Vector3 P1 = center + Vector3{ radius, height / 3.0f, radius };
						Vector3 P2 = center + Vector3{ -radius, 2.0f * height / 3.0f, -radius };

						// ベジエ曲線上の点を計算
						Vector3 position = BezierCurve(t, P0, P1, P2, P3);

						// 回転を加える (螺旋形状)
						float angle = rotations * 360.0f * t;
						float radians = angle * (pi / 180.0f);
						position.x += radius * cos(radians) * t;
						position.z += radius * sin(radians) * t;

						// 一時的なエミッターでパーティクルを生成
						Emitter tempEmitter = emitter5;
						tempEmitter.transform.translate = position; // 計算した位置に設定
						particles.splice(particles.end(), EmitSlashParticles(tempEmitter, randomEngine, position, 0.1f));
					}
				}

				if (ImGui::Button("Flame Breath: Unknowing Fire"))
				{
					int steps = 50; // 円弧の細かさ
					float radius = 10.0f; // 円弧の半径
					Vector3 center = emitter6.transform.translate; // エミッター中心位置

					for (int i = 0; i < steps; ++i)
					{
						float angle = (pi / 2.0f) * static_cast<float>(i) / static_cast<float>(steps - 1) - (pi / 4.0f); // -45°から+45°の範囲
						Vector3 position = center + Vector3{ radius * cos(angle), 0.0f, radius * sin(angle) };

						// 一時的なエミッターでパーティクルを生成
						Emitter tempEmitter = emitter6;
						tempEmitter.transform.translate = position;
						particles.splice(particles.end(), EmitFlameParticles(tempEmitter, randomEngine, position, 0.2f));
					}
				}


				if (ImGui::Button("Water Breathing: Calm"))
				{
					int steps = 30; // 円のパーティクル数
					float radius = 5.0f; // 円の半径
					Vector3 center = emitter7.transform.translate; // 中心位置

					for (int i = 0; i < steps; ++i)
					{
						float angle = (2.0f * pi) * static_cast<float>(i) / static_cast<float>(steps);
						Vector3 position = center + Vector3{ radius * cos(angle), 0.0f, radius * sin(angle) };

						// 一時的なエミッターで静止パーティクルを生成
						Emitter tempEmitter = emitter7;
						tempEmitter.transform.translate = position;
						particles.splice(particles.end(), EmitWaterParticles(tempEmitter, randomEngine, position, 0.0f));
					}
				}

				if (ImGui::Button("Thunder Breathing: Thunderclap and Flash"))
				{
					int steps = 20; // 雷の軌跡の細かさ
					float distance = 15.0f; // 移動距離
					Vector3 start = emitter8.transform.translate; // 開始位置
					Vector3 end = start + Vector3{ 0.0f, 0.0f, -distance }; // 終了位置

					for (int i = 0; i < steps; ++i)
					{
						float t = static_cast<float>(i) / static_cast<float>(steps - 1);
						Vector3 position = Lerp(start, end, t); // 線形補間

						// 雷のノイズを加える
						position.x += (randomEngine() % 200 - 100) / 100.0f * 0.5f;
						position.y += (randomEngine() % 200 - 100) / 100.0f * 0.5f;

						// 一時的なエミッターで雷のパーティクルを生成
						Emitter tempEmitter = emitter8;
						tempEmitter.transform.translate = position;
						particles.splice(particles.end(), EmitLightningParticles(tempEmitter, randomEngine, position, 0.05f));
					}
				}

				if (ImGui::Button("Wind Breathing: Clean Storm Wind Tree"))
				{
					int steps = 50; // 円形の細かさ
					float maxRadius = 20.0f; // 最大半径
					Vector3 center = emitter9.transform.translate; // 中心位置

					for (float r = 0.0f; r <= maxRadius; r += 2.0f) // 半径を増やす
					{
						for (int i = 0; i < steps; ++i)
						{
							float angle = (2.0f * pi) * static_cast<float>(i) / static_cast<float>(steps);
							Vector3 position = center + Vector3{ r * cos(angle), 0.0f, r * sin(angle) };

							// 一時的なエミッターで風のパーティクルを生成
							Emitter tempEmitter = emitter9;
							tempEmitter.transform.translate = position;
							particles.splice(particles.end(), EmitWindParticles(tempEmitter, randomEngine, position, 0.05f));
						}
					}
				}

				ImGui::DragFloat3("EmitterTranslate", &emitter.transform.translate.x, 0.01f, -100.0f, 100.0f);

				ImGui::End();
			}

			//ImGuiの内部コマンドを生成する
			ImGui::Render();

			///-----ゲームの処理-----///

			//transform.rotate.y += 0.03f;

			/*-----Transform情報を作る-----*/

			// カメラの回転を適用する
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

			// ビルボード
			Matrix4x4 billboardMatrix = Multiply(backToFrontMatrix, cameraMatrix);
			billboardMatrix.m[3][0] = 0.0f; // 平行移動成分はいらない
			billboardMatrix.m[3][1] = 0.0f;
			billboardMatrix.m[3][2] = 0.0f;

			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

			wvpData->WVP = worldViewProjectionMatrix;
			wvpData->World = worldMatrix;

			numInstance = 0;

			// 板ポリ
			for (std::list<Particle>::iterator particleIterator = particles.begin(); particleIterator != particles.end(); )
			{
				if ((*particleIterator).lifeTime <= (*particleIterator).currentTIme)
				{
					// 生存時間を過ぎていたら更新せず描画対象にしない
					particleIterator = particles.erase(particleIterator);
					continue;
				}

				// worldMatrixを求める
				Matrix4x4 worldMatrix = MakeAffineMatrix((*particleIterator).transform.scale, (*particleIterator).transform.rotate, (*particleIterator).transform.translate);
				scaleMatrix = MakeScaleMatrix((*particleIterator).transform.scale);
				translateMatrix = MakeTranslateMatrix((*particleIterator).transform.translate);

				// ビルボードを使うかどうか
				if (useBillboard)
				{
					worldMatrix = scaleMatrix * billboardMatrix * translateMatrix;
				}

				Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
				instancingData[numInstance].WVP = worldViewProjectionMatrix;
				instancingData[numInstance].World = worldMatrix;

				float alpha = 1.0f - ((*particleIterator).currentTIme / (*particleIterator).lifeTime);

				// パーティクルの動き
				if (isMove)
				{
					// 速度を適用
					(*particleIterator).transform.translate += (*particleIterator).velocity * kDeltaTime;
					(*particleIterator).currentTIme += kDeltaTime; // 経過時間を足す
				}

				if (isWind)
				{
					// フィールドの範囲内のパーティクルには加速度を適用する
						// 各パーティクルに最適な風を適用
					for (const auto& zone : windZones) {
						if (IsCollision(accelerationField.area, (*particleIterator).transform.translate)) {
							(*particleIterator).velocity.x += zone.strength.x;
							(*particleIterator).velocity.y += zone.strength.y;
							(*particleIterator).velocity.z += zone.strength.z;
						}
					}

				}

				// 最大描画数を超えないようにする
				if (numInstance < kNumMaxInstance)
				{
					instancingData[numInstance].WVP = worldViewProjectionMatrix;
					instancingData[numInstance].World = worldMatrix;
					instancingData[numInstance].color = (*particleIterator).color;
					instancingData[numInstance].color.w = alpha;
					++numInstance; // 生きているParticleの数を1つカウントする
				}

				++particleIterator; // 次のパーティクルに進める
			}

			if (isParticle)
			{
				// 頻度によって発生させる
				emitter.frequencyTime += kDeltaTime; // 時刻を進める

				// 頻度より大きいなら発生
				if (emitter.frequency <= emitter.frequencyTime)
				{
					particles.splice(particles.end(), Emit(emitter, randomEngine)); // 発生処理
					emitter.frequencyTime -= emitter.frequency; // 余計に過ぎた時間も加味して頻度計算する
				}
			}

			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

#pragma region リソースの状態を遷移させ描画ターゲットを設定しクリア操作を実行
			//TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			//今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			//Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			//バリアを春対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
			//遷移前（現在）のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			//遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			//TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			//指定した色で画面全体をクリアする
			float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };	//青っぽい色。RGBAの順
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			//指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
#pragma endregion


			/*-----ImGuiを描画する-----*/
			//描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			/*-----ImGuiを描画する-----*/


#pragma region 描画コマンドを設定し三角形とスプライトを描画する一連の操作を行う
			//ビューポートとシザー矩形の設定
			commandList->RSSetViewports(1, &viewport);					//Viewportを設定
			commandList->RSSetScissorRects(1, &scissorRect);			//Scissor

			//ルートシグネチャとパイプラインステートの設定
			commandList->SetGraphicsRootSignature(rootSignature.Get());		// ルートシグネチャを設定
			commandList->SetPipelineState(graphicsPipelineState.Get());		// パイプラインステートオブジェクト (PSO) を設定

			//頂点バッファの設定とプリミティブトポロジの設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);					//VBVを設定
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);	//プリミティブトポロジを設定

			//定数バッファビュー (CBV) とディスクリプタテーブルの設定
			//マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());					// マテリアルCBVを設定

			commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);

			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);	// SRVのディスクリプタテーブルを設定

			commandList->DrawInstanced(UINT(modelData.vertices.size()), numInstance, 0, 0);								// 描画コール。三角形を描画(頂点数を変えれば球体が出るようになる「TotalVertexCount」)

			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

#pragma endregion


			/*-----ImGuiを描画する-----*/
			//実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());


#pragma region レンダーターゲットからプレゼント状態にリソースを遷移させその後コマンドリストをクローズ
			//画面に描く処理はすべて終わり、画面に移すので、状態を遷移
			//今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			//TransitionBarirrerを張る
			commandList->ResourceBarrier(1, &barrier);

			//コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));
#pragma endregion


#pragma region コマンドをキックするその後に画面の表示を更新する操作を続けて行っている
			//GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList.Get() };
			//GPUに対して積まれたコマンドを実行
			commandQueue->ExecuteCommandLists(1, commandLists);
			//GPUとOSに画面の交換を行うよう通知する
			swapChain->Present(1, 0);
#pragma endregion


#pragma region GPUのコマンド実行が完了するまで待機するための同期処理を行いその後次のフレームのためにコマンドリストをリセット
			//Fenceの値を更新
			fenceValue++;
			//GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			//Fenceの値が指定したSignal値にたどり着いているか確認する
			//GetCompletedValueの初期値はFence作成時にわあ足した初期値
			if (fence->GetCompletedValue() < fenceValue)
			{
				//指定したSignalにたどりついていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				//イベントを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			//次のフレーム用のコマンドリストを準備（コマンドリストのリセット）
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
#pragma endregion
		}
	}

#pragma region メモリリークしないための解放処理
	CloseHandle(fenceEvent);
	CloseWindow(hwnd);
#pragma endregion

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	//COMの終了処理
	CoUninitialize();
	return 0;
}
