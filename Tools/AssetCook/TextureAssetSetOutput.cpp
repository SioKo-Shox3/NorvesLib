#include "TextureAssetSetOutput.h"
#include <charconv>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace
    {
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        bool Escape(AnsiStringView text, AnsiString& out)
        {
            constexpr char hex[]="0123456789abcdef";
            out.push_back('"');
            for (const unsigned char c:text)
            {
                // v1単体CLIのmanifest fieldはprintable ASCII。別の文字集合を黙って変換しない。
                if (c<32 || c>=127) return false;
                if (c=='"' || c=='\\') { out.push_back('\\');out.push_back(static_cast<char>(c)); }
                else if (c=='\'' || c=='<' || c=='>' || c=='&')
                {
                    out.append("\\u00");out.push_back(hex[c>>4]);out.push_back(hex[c&15]);
                }
                else out.push_back(static_cast<char>(c));
            }
            out.push_back('"');return true;
        }
    }
    bool SerializeLegacyTextureManifest(Core::Container::Span<const Core::Asset::AssetCookedReference> references,
        AnsiString& output, AnsiString& error)
    {
        if (references.empty()) { error="texture_asset_set: empty aggregate";return false; }
        AnsiString candidate="{\r\n    \"version\":  1,\r\n    \"assets\":  [\r\n";
        for (size_t i=0;i<references.size();++i)
        {
            const auto& row=references[i];
            if (row.Kind!=Core::Asset::AssetKind::Texture || row.CookedVersion!=0 || row.bHasSkeletalMetadata ||
                row.EntryType!=Core::Asset::MakeAssetPackageFourCC('T','e','x','0'))
            { error="texture_asset_set: unsupported aggregate reference";return false; }
            candidate.append("                   {\r\n");
            const auto sourceHash=Core::Asset::FormatAssetHashHex(row.SourceHash);
            const auto cookedHash=Core::Asset::FormatAssetHashHex(row.CookedHash);
            struct Field { const char* Key; AnsiStringView Value; };
            const Field fields[]={{"logical_path",row.LogicalPath},{"kind","texture"},{"source_hash",sourceHash},
                {"variant",row.Variant},{"format",row.Format},{"cooked_package",row.CookedPackage},
                {"entry_name",row.EntryName},{"entry_type","Tex0"},{"cooked_hash",cookedHash}};
            for (const auto& field:fields)
            {
                candidate.append("                       \"");candidate.append(field.Key);candidate.append("\":  ");
                if (!Escape(field.Value,candidate)) { error="texture_asset_set: non-ASCII aggregate field";return false; }
                candidate.append(",\r\n");
            }
            candidate.append("                       \"cooked_version\":  0\r\n                   }");
            candidate.append(i+1<references.size()?",\r\n":"\r\n");
        }
        candidate.append("               ]\r\n}");
        output=std::move(candidate);error.clear();return true;
    }
    bool PublishNewTextureAssetSet(const std::filesystem::path& stage,
        const std::filesystem::path& destination, AnsiString& error)
    {
#if defined(_WIN32)
        // REPLACE_EXISTINGもCOPY_ALLOWEDも使わず、同volumeでの新規directory公開だけ許す。
        if (!MoveFileExW(stage.c_str(),destination.c_str(),0))
        {
            const auto code=GetLastError();char digits[32];
            const auto converted=std::to_chars(digits,digits+sizeof(digits),code);
            error="texture_asset_set: publish refused, win32=";error.append(digits,static_cast<size_t>(converted.ptr-digits));
            return false;
        }
        error.clear();return true;
#else
        (void)stage;(void)destination;
        error="texture_asset_set: no-replace directory publication requires Windows";return false;
#endif
    }
}
