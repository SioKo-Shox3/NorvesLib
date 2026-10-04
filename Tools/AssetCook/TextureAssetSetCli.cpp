#include "TextureAssetSetCook.h"
#include "Text/UnicodeText.h"
#include <cstdio>
#include <cstring>
namespace NorvesLib::Tools::AssetCook
{
    bool RunTextureAssetSetCommand(int argc,const char* const* argv,int& exitCode)
    {
        bool found=false;
        for (int i=1;i<argc;++i)
            if (std::strcmp(argv[i],"--asset-set")==0 || std::strncmp(argv[i],"--asset-set=",12)==0) found=true;
        if (!found) return false;
        const auto reject=[&](const char* reason)
        { std::fprintf(stderr,"AssetCook error: texture_asset_set: %s\n",reason);exitCode=1;return true; };
        TextureAssetSetCookRequest request;
        bool seen[4]={};
        const char* names[]={"--asset-set","--source-root","--runtime-root","--manifest"};
        std::filesystem::path* paths[]={&request.SpecPath,&request.SourceRoot,&request.RuntimeRoot,&request.ManifestPath};
        try
        {
            for (int i=1;i<argc;++i)
            {
                const char* arg=argv[i];const char* equals=std::strchr(arg,'=');
                const size_t length=equals?static_cast<size_t>(equals-arg):std::strlen(arg);
                size_t option=4;
                for (size_t n=0;n<4;++n) if (length==std::strlen(names[n]) && std::memcmp(arg,names[n],length)==0) option=n;
                if (option==4) return reject("unknown or mixed option");
                if (seen[option]) return reject("duplicate option");
                seen[option]=true;
                const char* value=equals?equals+1:(i+1<argc?argv[++i]:nullptr);
                if (!value || !*value || std::strncmp(value,"--",2)==0) return reject("missing option value");
                if (!Core::TextDetail::ForEachUnicodeScalar<char>({value,std::strlen(value)},[](uint32_t){})) return reject("path argument is not UTF-8");
                *paths[option]=std::filesystem::u8path(value,value+std::strlen(value));
            }
            if (!seen[0] || !seen[2]) return reject("--asset-set and --runtime-root are required");
            Core::Container::AnsiString error;
            if (!CookTextureAssetSet(request,error)) return reject(error.c_str());
            std::fprintf(stderr,"AssetCook texture_asset_set published\n");exitCode=0;return true;
        }
        catch (const std::exception& exception) { return reject(exception.what()); }
    }
}
