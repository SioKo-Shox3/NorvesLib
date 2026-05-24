#include "Resource/GLTFLoader.h"
#include "Logging/LogMacros.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>

namespace NorvesLib::Core::Resource
{
    // ========================================
    // 簡易JSONパーサーヘルパー（glTF専用）
    // ========================================

    namespace
    {
        /**
         * @brief JSON文字列内で指定キーの値の開始位置を返す
         * @param json JSON文字列
         * @param key 検索キー（ダブルクォート不要）
         * @param startPos 検索開始位置
         * @return 値の開始位置（見つからない場合 String::npos）
         */
        size_t FindJsonValue(const String& json, const char* key, size_t startPos = 0)
        {
            String searchKey = String("\"") + key + "\"";
            size_t keyPos = json.find(searchKey, startPos);
            if (keyPos == String::npos)
            {
                return String::npos;
            }
            // キー後の ':' を探す
            size_t colonPos = json.find(':', keyPos + searchKey.size());
            if (colonPos == String::npos)
            {
                return String::npos;
            }
            // ':' 後の空白をスキップ
            size_t valuePos = colonPos + 1;
            while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\n' || json[valuePos] == '\r' || json[valuePos] == '\t'))
            {
                ++valuePos;
            }
            return valuePos;
        }

        /**
         * @brief JSON値から整数を読み取る
         */
        int ParseJsonInt(const String& json, const char* key, size_t startPos = 0, int defaultValue = 0)
        {
            size_t pos = FindJsonValue(json, key, startPos);
            if (pos == String::npos)
            {
                return defaultValue;
            }
            return std::atoi(json.c_str() + pos);
        }

        /**
         * @brief JSON値から文字列を読み取る（ダブルクォートで囲まれた値）
         */
        String ParseJsonString(const String& json, const char* key, size_t startPos = 0)
        {
            size_t pos = FindJsonValue(json, key, startPos);
            if (pos == String::npos || json[pos] != '"')
            {
                return "";
            }
            size_t endPos = json.find('"', pos + 1);
            if (endPos == String::npos)
            {
                return "";
            }
            return json.substr(pos + 1, endPos - pos - 1);
        }

        /**
         * @brief JSON配列 '[' の開始位置から対応する ']' までの範囲を返す
         */
        size_t FindJsonArrayEnd(const String& json, size_t startPos)
        {
            if (startPos >= json.size() || json[startPos] != '[')
            {
                return String::npos;
            }
            int depth = 0;
            for (size_t i = startPos; i < json.size(); ++i)
            {
                if (json[i] == '[')
                {
                    ++depth;
                }
                else if (json[i] == ']')
                {
                    --depth;
                    if (depth == 0)
                    {
                        return i;
                    }
                }
            }
            return String::npos;
        }

        /**
         * @brief JSON配列から各オブジェクト要素の開始・終了位置を収集
         * @param json JSON文字列
         * @param arrayStart '[' の位置
         * @param arrayEnd ']' の位置
         * @param outStarts 各要素の '{' 位置
         * @param outEnds 各要素の '}' 位置
         */
        void EnumerateJsonObjects(const String& json, size_t arrayStart, size_t arrayEnd,
                                  VariableArray<size_t>& outStarts, VariableArray<size_t>& outEnds)
        {
            int depth = 0;
            size_t objStart = 0;
            for (size_t i = arrayStart + 1; i < arrayEnd; ++i)
            {
                if (json[i] == '{')
                {
                    if (depth == 0)
                    {
                        objStart = i;
                    }
                    ++depth;
                }
                else if (json[i] == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        outStarts.push_back(objStart);
                        outEnds.push_back(i);
                    }
                }
            }
        }

        /**
         * @brief JSON値からboolを読み取る
         */
        bool ParseJsonBool(const String& json, const char* key, size_t startPos = 0, bool defaultValue = false)
        {
            size_t pos = FindJsonValue(json, key, startPos);
            if (pos == String::npos)
            {
                return defaultValue;
            }
            if (std::strncmp(json.c_str() + pos, "true", 4) == 0)
            {
                return true;
            }
            if (std::strncmp(json.c_str() + pos, "false", 5) == 0)
            {
                return false;
            }
            return defaultValue;
        }

        /**
         * @brief アセット相対パスを実行時のアセットルート基準へ解決
         */
        String ResolveAssetPath(const String& path)
        {
            String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
            if (!path.empty() &&
                path[0] != '/' && path[0] != '\\' &&
                (path.size() < 2 || path[1] != ':'))
            {
                String relativePath = path;
                if (relativePath.size() > 7)
                {
                    String prefix = relativePath.substr(0, 7);
                    if (prefix == "Assets/" || prefix == "Assets\\")
                    {
                        relativePath = relativePath.substr(7);
                    }
                }
                resolvedPath = String(NORVES_ASSET_DIR) + "/" + relativePath;
            }
#endif
            return resolvedPath;
        }

    } // anonymous namespace

    // ========================================
    // GLTFLoader 実装
    // ========================================

    GLTFLoadResult GLTFLoader::Load(const String& gltfPath)
    {
        GLTFLoadResult result;
        result.bSuccess = false;
        String resolvedGltfPath = ResolveAssetPath(gltfPath);

        // ========================================
        // 1. glTF JSONファイルの読み込み
        // ========================================
        String jsonContent;
        {
            std::ifstream file(resolvedGltfPath.c_str(), std::ios::in);
            if (!file.is_open())
            {
                NORVES_LOG_ERROR("GLTFLoader", "glTFファイルを開けません: %s", resolvedGltfPath.c_str());
                return result;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            jsonContent = ss.str();
        }

        // glTFファイルのディレクトリパスを取得
        std::filesystem::path gltfDir = std::filesystem::path(resolvedGltfPath.c_str()).parent_path();

        // ========================================
        // 2. アクセサの解析
        // ========================================
        struct AccessorInfo
        {
            uint32_t BufferView = 0;
            uint32_t ComponentType = 0; // 5126=float, 5125=uint32
            uint32_t Count = 0;
            String Type; // "VEC3", "VEC2", "SCALAR"
        };
        VariableArray<AccessorInfo> accessors;

        {
            size_t accessorsPos = FindJsonValue(jsonContent, "accessors");
            if (accessorsPos == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "accessorsが見つかりません");
                return result;
            }
            size_t arrayEnd = FindJsonArrayEnd(jsonContent, accessorsPos);
            if (arrayEnd == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "accessors配列の終端が見つかりません");
                return result;
            }

            VariableArray<size_t> objStarts, objEnds;
            EnumerateJsonObjects(jsonContent, accessorsPos, arrayEnd, objStarts, objEnds);

            for (size_t i = 0; i < objStarts.size(); ++i)
            {
                AccessorInfo acc;
                acc.BufferView = static_cast<uint32_t>(ParseJsonInt(jsonContent, "bufferView", objStarts[i]));
                acc.ComponentType = static_cast<uint32_t>(ParseJsonInt(jsonContent, "componentType", objStarts[i]));
                acc.Count = static_cast<uint32_t>(ParseJsonInt(jsonContent, "count", objStarts[i]));
                acc.Type = ParseJsonString(jsonContent, "type", objStarts[i]);
                accessors.push_back(acc);
            }
        }

        // ========================================
        // 3. バッファビューの解析
        // ========================================
        struct BufferViewInfo
        {
            uint32_t Buffer = 0;
            size_t ByteLength = 0;
            size_t ByteOffset = 0;
        };
        VariableArray<BufferViewInfo> bufferViews;

        {
            size_t bvPos = FindJsonValue(jsonContent, "bufferViews");
            if (bvPos == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "bufferViewsが見つかりません");
                return result;
            }
            size_t arrayEnd = FindJsonArrayEnd(jsonContent, bvPos);
            if (arrayEnd == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "bufferViews配列の終端が見つかりません");
                return result;
            }

            VariableArray<size_t> objStarts, objEnds;
            EnumerateJsonObjects(jsonContent, bvPos, arrayEnd, objStarts, objEnds);

            for (size_t i = 0; i < objStarts.size(); ++i)
            {
                BufferViewInfo bv;
                bv.Buffer = static_cast<uint32_t>(ParseJsonInt(jsonContent, "buffer", objStarts[i]));
                bv.ByteLength = static_cast<size_t>(ParseJsonInt(jsonContent, "byteLength", objStarts[i]));
                bv.ByteOffset = static_cast<size_t>(ParseJsonInt(jsonContent, "byteOffset", objStarts[i]));
                bufferViews.push_back(bv);
            }
        }

        // ========================================
        // 4. バッファ（.bin）の読み込み
        // ========================================
        VariableArray<uint8_t> binData;
        {
            size_t buffersPos = FindJsonValue(jsonContent, "buffers");
            if (buffersPos == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "buffersが見つかりません");
                return result;
            }
            size_t arrayEnd = FindJsonArrayEnd(jsonContent, buffersPos);
            if (arrayEnd == String::npos)
            {
                NORVES_LOG_ERROR("GLTFLoader", "buffers配列の終端が見つかりません");
                return result;
            }

            VariableArray<size_t> objStarts, objEnds;
            EnumerateJsonObjects(jsonContent, buffersPos, arrayEnd, objStarts, objEnds);

            if (objStarts.empty())
            {
                NORVES_LOG_ERROR("GLTFLoader", "バッファが見つかりません");
                return result;
            }

            String binUri = ParseJsonString(jsonContent, "uri", objStarts[0]);
            size_t byteLength = static_cast<size_t>(ParseJsonInt(jsonContent, "byteLength", objStarts[0]));

            std::filesystem::path binPath = gltfDir / binUri.c_str();
            std::ifstream binFile(binPath, std::ios::binary);
            if (!binFile.is_open())
            {
                NORVES_LOG_ERROR("GLTFLoader", "バイナリファイルを開けません: %s", binPath.string().c_str());
                return result;
            }

            binData.resize(byteLength);
            binFile.read(reinterpret_cast<char*>(binData.data()), static_cast<std::streamsize>(byteLength));
            if (static_cast<size_t>(binFile.gcount()) != byteLength)
            {
                NORVES_LOG_ERROR("GLTFLoader", "バイナリファイルの読み込みが不完全です");
                return result;
            }
        }

        // ========================================
        // 5. メッシュプリミティブの属性インデックス取得
        // ========================================
        // このローダーは最初のメッシュの最初のプリミティブのみをロードする
        uint32_t posAccessor = 0;
        uint32_t normalAccessor = 1;
        uint32_t texcoordAccessor = 2;
        uint32_t indexAccessor = 3;

        {
            // meshes[0].primitives[0].attributes からアクセサインデックスを取得
            size_t meshesPos = FindJsonValue(jsonContent, "meshes");
            if (meshesPos != String::npos)
            {
                size_t attrPos = FindJsonValue(jsonContent, "attributes", meshesPos);
                if (attrPos != String::npos)
                {
                    // POSITION, NORMAL, TEXCOORD_0 のアクセサインデックスを取得
                    int posIdx = ParseJsonInt(jsonContent, "POSITION", attrPos, -1);
                    int normIdx = ParseJsonInt(jsonContent, "NORMAL", attrPos, -1);
                    int texIdx = ParseJsonInt(jsonContent, "TEXCOORD_0", attrPos, -1);

                    if (posIdx >= 0)
                    {
                        posAccessor = static_cast<uint32_t>(posIdx);
                    }
                    if (normIdx >= 0)
                    {
                        normalAccessor = static_cast<uint32_t>(normIdx);
                    }
                    if (texIdx >= 0)
                    {
                        texcoordAccessor = static_cast<uint32_t>(texIdx);
                    }
                }

                // indices アクセサインデックス
                // "primitives" 配列内の最初のオブジェクトを探す
                size_t primPos = FindJsonValue(jsonContent, "primitives", meshesPos);
                if (primPos != String::npos)
                {
                    int indIdx = ParseJsonInt(jsonContent, "indices", primPos, -1);
                    if (indIdx >= 0)
                    {
                        indexAccessor = static_cast<uint32_t>(indIdx);
                    }
                }

                // メッシュ名を取得
                result.MeshName = ParseJsonString(jsonContent, "name", meshesPos);
            }
        }

        // ========================================
        // 6. 頂点データのインターリーブ
        // ========================================
        if (posAccessor >= accessors.size() || normalAccessor >= accessors.size() || texcoordAccessor >= accessors.size())
        {
            NORVES_LOG_ERROR("GLTFLoader", "アクセサインデックスが不正です");
            return result;
        }

        const auto& posAcc = accessors[posAccessor];
        const auto& normAcc = accessors[normalAccessor];
        const auto& texAcc = accessors[texcoordAccessor];

        if (posAcc.Count != normAcc.Count || posAcc.Count != texAcc.Count)
        {
            NORVES_LOG_ERROR("GLTFLoader", "頂点属性のカウントが不一致です (pos=%u, norm=%u, tex=%u)",
                             posAcc.Count, normAcc.Count, texAcc.Count);
            return result;
        }

        uint32_t vertexCount = posAcc.Count;
        result.Vertices.resize(vertexCount);

        const auto& posBV = bufferViews[posAcc.BufferView];
        const auto& normBV = bufferViews[normAcc.BufferView];
        const auto& texBV = bufferViews[texAcc.BufferView];

        const float* posData = reinterpret_cast<const float*>(binData.data() + posBV.ByteOffset);
        const float* normData = reinterpret_cast<const float*>(binData.data() + normBV.ByteOffset);
        const float* texData = reinterpret_cast<const float*>(binData.data() + texBV.ByteOffset);

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            Mesh3DVertex& v = result.Vertices[i];
            v.Position[0] = posData[i * 3 + 0];
            v.Position[1] = posData[i * 3 + 1];
            v.Position[2] = posData[i * 3 + 2];
            v.Normal[0] = normData[i * 3 + 0];
            v.Normal[1] = normData[i * 3 + 1];
            v.Normal[2] = normData[i * 3 + 2];
            v.TexCoord[0] = texData[i * 2 + 0];
            v.TexCoord[1] = texData[i * 2 + 1];
        }

        // ========================================
        // 7. インデックスデータの読み取り
        // ========================================
        if (indexAccessor >= accessors.size())
        {
            NORVES_LOG_ERROR("GLTFLoader", "インデックスアクセサが不正です");
            return result;
        }

        const auto& idxAcc = accessors[indexAccessor];
        const auto& idxBV = bufferViews[idxAcc.BufferView];
        uint32_t indexCount = idxAcc.Count;
        result.Indices.resize(indexCount);

        if (idxAcc.ComponentType == 5125) // uint32
        {
            const uint32_t* idxData = reinterpret_cast<const uint32_t*>(binData.data() + idxBV.ByteOffset);
            std::memcpy(result.Indices.data(), idxData, indexCount * sizeof(uint32_t));
        }
        else if (idxAcc.ComponentType == 5123) // uint16
        {
            const uint16_t* idxData = reinterpret_cast<const uint16_t*>(binData.data() + idxBV.ByteOffset);
            for (uint32_t i = 0; i < indexCount; ++i)
            {
                result.Indices[i] = static_cast<uint32_t>(idxData[i]);
            }
        }
        else
        {
            NORVES_LOG_ERROR("GLTFLoader", "未対応のインデックスコンポーネントタイプ: %u", idxAcc.ComponentType);
            return result;
        }

        // ========================================
        // 8. テクスチャ情報の抽出
        // ========================================
        {
            // images配列のURIを収集
            VariableArray<String> imageUris;
            {
                size_t imagesPos = FindJsonValue(jsonContent, "images");
                if (imagesPos != String::npos)
                {
                    size_t arrayEnd = FindJsonArrayEnd(jsonContent, imagesPos);
                    if (arrayEnd != String::npos)
                    {
                        VariableArray<size_t> objStarts, objEnds;
                        EnumerateJsonObjects(jsonContent, imagesPos, arrayEnd, objStarts, objEnds);
                        for (size_t i = 0; i < objStarts.size(); ++i)
                        {
                            imageUris.push_back(ParseJsonString(jsonContent, "uri", objStarts[i]));
                        }
                    }
                }
            }

            // textures配列のsource → imagesインデックス
            VariableArray<uint32_t> textureSources;
            {
                size_t texturesPos = FindJsonValue(jsonContent, "textures");
                if (texturesPos != String::npos)
                {
                    size_t arrayEnd = FindJsonArrayEnd(jsonContent, texturesPos);
                    if (arrayEnd != String::npos)
                    {
                        VariableArray<size_t> objStarts, objEnds;
                        EnumerateJsonObjects(jsonContent, texturesPos, arrayEnd, objStarts, objEnds);
                        for (size_t i = 0; i < objStarts.size(); ++i)
                        {
                            textureSources.push_back(
                                static_cast<uint32_t>(ParseJsonInt(jsonContent, "source", objStarts[i])));
                        }
                    }
                }
            }

            // materials[0] からテクスチャマッピングを読み取る
            size_t materialsPos = FindJsonValue(jsonContent, "materials");
            if (materialsPos != String::npos)
            {
                size_t arrayEnd = FindJsonArrayEnd(jsonContent, materialsPos);
                if (arrayEnd != String::npos)
                {
                    VariableArray<size_t> matStarts, matEnds;
                    EnumerateJsonObjects(jsonContent, materialsPos, arrayEnd, matStarts, matEnds);

                    if (!matStarts.empty())
                    {
                        size_t matStart = matStarts[0];
                        size_t matEnd = matEnds[0];

                        // doubleSided
                        result.bDoubleSided = ParseJsonBool(jsonContent, "doubleSided", matStart, false);

                        // normalTexture
                        {
                            size_t ntPos = FindJsonValue(jsonContent, "normalTexture", matStart);
                            if (ntPos != String::npos && ntPos < matEnd)
                            {
                                int texIdx = ParseJsonInt(jsonContent, "index", ntPos, -1);
                                if (texIdx >= 0 && static_cast<uint32_t>(texIdx) < textureSources.size())
                                {
                                    uint32_t imgIdx = textureSources[texIdx];
                                    if (imgIdx < imageUris.size())
                                    {
                                        GLTFTextureInfo info;
                                        info.FilePath = String((gltfDir / imageUris[imgIdx].c_str()).string().c_str());
                                        info.Type = GLTFTextureType::Normal;
                                        result.Textures.push_back(info);
                                    }
                                }
                            }
                        }

                        // pbrMetallicRoughness
                        {
                            size_t pbrPos = FindJsonValue(jsonContent, "pbrMetallicRoughness", matStart);
                            if (pbrPos != String::npos && pbrPos < matEnd)
                            {
                                // baseColorTexture
                                size_t bctPos = FindJsonValue(jsonContent, "baseColorTexture", pbrPos);
                                if (bctPos != String::npos && bctPos < matEnd)
                                {
                                    int texIdx = ParseJsonInt(jsonContent, "index", bctPos, -1);
                                    if (texIdx >= 0 && static_cast<uint32_t>(texIdx) < textureSources.size())
                                    {
                                        uint32_t imgIdx = textureSources[texIdx];
                                        if (imgIdx < imageUris.size())
                                        {
                                            GLTFTextureInfo info;
                                            info.FilePath = String((gltfDir / imageUris[imgIdx].c_str()).string().c_str());
                                            info.Type = GLTFTextureType::Albedo;
                                            result.Textures.push_back(info);
                                        }
                                    }
                                }

                                // metallicRoughnessTexture → ARM
                                size_t mrtPos = FindJsonValue(jsonContent, "metallicRoughnessTexture", pbrPos);
                                if (mrtPos != String::npos && mrtPos < matEnd)
                                {
                                    int texIdx = ParseJsonInt(jsonContent, "index", mrtPos, -1);
                                    if (texIdx >= 0 && static_cast<uint32_t>(texIdx) < textureSources.size())
                                    {
                                        uint32_t imgIdx = textureSources[texIdx];
                                        if (imgIdx < imageUris.size())
                                        {
                                            GLTFTextureInfo info;
                                            info.FilePath = String((gltfDir / imageUris[imgIdx].c_str()).string().c_str());
                                            info.Type = GLTFTextureType::ARM;
                                            result.Textures.push_back(info);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        result.bSuccess = true;
        NORVES_LOG_INFO("GLTFLoader", "glTFロード完了: %s (%u vertices, %u indices, %zu textures)",
                        result.MeshName.c_str(), vertexCount, indexCount, result.Textures.size());

        return result;
    }

} // namespace NorvesLib::Core::Resource
