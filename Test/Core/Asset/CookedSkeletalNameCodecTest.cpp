#include "Asset/CookedSkeletalNameCodec.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Asset;
using NorvesLib::Core::Container::Span;
namespace
{
    void Failed(const SkeletalNameCodecResult& result, SkeletalNameStatus status)
    {
        assert(!result.Succeeded() && result.Status==status && result.ByteCount==0 && result.CodeUnitCount==0);
    }
}
int main()
{
    using Status=SkeletalNameStatus;
    const char ascii[]="Body";
    uint8_t out[64]; std::memset(out,0xa5,sizeof(out));
    for(uint16_t minor:{uint16_t{0},uint16_t{1},uint16_t{2}})
    {
        const auto measured=MeasureSkeletalNameEncoding<char>(minor,{ascii,4});
        const auto encoded=EncodeSkeletalWireName<char>(minor,{ascii,4},out);
        assert(measured.Succeeded() && measured.ByteCount==4 && measured.CodeUnitCount==4 && encoded.Succeeded());
        assert(std::memcmp(out,ascii,4)==0 && out[4]==0xa5);
        char decoded[5]={'!','!','!','!','!'};
        const auto result=DecodeSkeletalWireName<char>(minor,{out,4},decoded);
        assert(result.Succeeded() && result.CodeUnitCount==4 && std::memcmp(decoded,ascii,4)==0 && decoded[4]=='!');
    }
    // 日本語1字+補助平面1字。UTF-8は7byte、UTF-16は3unit、UTF-32は2unit。
    const uint8_t text[]={0xe9,0xaa,0xa8,0xf0,0x9f,0x90,0xba};
    const char16_t utf16[]={0x9aa8,0xd83d,0xdc3a};
    const char32_t utf32[]={0x9aa8,0x1f43a};
    assert(EncodeSkeletalWireName<char16_t>(2,utf16,out).ByteCount==7 && std::memcmp(out,text,7)==0);
    assert(EncodeSkeletalWireName<char32_t>(2,utf32,out).ByteCount==7 && std::memcmp(out,text,7)==0);
    const auto measured16=MeasureSkeletalNameDecoding<char16_t>(2,text);
    const auto measured32=MeasureSkeletalNameDecoding<char32_t>(2,text);
    assert(measured16.Succeeded() && measured16.ByteCount==7 && measured16.CodeUnitCount==3);
    assert(measured32.Succeeded() && measured32.CodeUnitCount==2);
    char16_t decoded16[4]={0,0,0,0x1234}; char32_t decoded32[3]={0,0,0x1234};
    assert(DecodeSkeletalWireName<char16_t>(2,text,decoded16).Succeeded() && std::memcmp(decoded16,utf16,sizeof(utf16))==0 && decoded16[3]==0x1234);
    assert(DecodeSkeletalWireName<char32_t>(2,text,decoded32).Succeeded() && std::memcmp(decoded32,utf32,sizeof(utf32))==0 && decoded32[2]==0x1234);
    wchar_t wide[4]={};
    const auto wideResult=DecodeSkeletalWireName<wchar_t>(2,text,wide);
    assert(wideResult.Succeeded() && wideResult.CodeUnitCount==(sizeof(wchar_t)==2 ? 3u : 2u));
    assert(EncodeSkeletalWireName<wchar_t>(2,{wide,wideResult.CodeUnitCount},out).Succeeded() && std::memcmp(out,text,7)==0);
    const char8_t narrow[]=u8"骨🐺";
    assert(EncodeSkeletalWireName<char8_t>(2,{narrow,7},out).Succeeded() && std::memcmp(out,text,7)==0);
    Failed(EncodeSkeletalWireName<char16_t>(1,utf16,out),Status::InvalidAscii);
    Failed(MeasureSkeletalNameDecoding<char>(0,text),Status::InvalidAscii);
    const uint8_t controls[]={0x0a};
    Failed(MeasureSkeletalNameDecoding<char>(1,controls),Status::InvalidAscii);
    assert(MeasureSkeletalNameDecoding<char>(2,controls).Succeeded());
    const uint8_t nul[]={0};
    Failed(MeasureSkeletalNameDecoding<char>(2,nul),Status::EmbeddedNul);
    Failed(MeasureSkeletalNameDecoding<char>(0,nul),Status::EmbeddedNul);
    const char16_t nul16[]={0};
    Failed(EncodeSkeletalWireName<char16_t>(2,nul16,out),Status::EmbeddedNul);
    const uint8_t bad[][4]={{0x80},{0xc0,0x80},{0xc1,0xbf},{0xe0,0x80,0x80},{0xed,0xa0,0x80},{0xf0,0x80,0x80,0x80},
        {0xf4,0x90,0x80,0x80},{0xf5,0x80,0x80,0x80},{0xff},{0xe2,0x28,0xa1},{0xf0,0x9f,0x90},{0xe9,0xaa},{0xc2}};
    const size_t lengths[]={1,2,2,3,3,4,4,4,1,3,3,2,1};
    for(size_t index=0;index<sizeof(lengths)/sizeof(lengths[0]);++index)
    {
        char32_t destination[4]={0x1234,0x1234,0x1234,0x1234};
        Failed(DecodeSkeletalWireName<char32_t>(2,{bad[index],lengths[index]},destination),Status::InvalidUtf8);
        for(auto unit:destination)
        {
            assert(unit==0x1234);
        }
    }
    const uint8_t partialBad[]={static_cast<uint8_t>('A'),0xe9,0xaa};
    char32_t untouched[]={0x1234,0x1234};
    Failed(DecodeSkeletalWireName<char32_t>(2,partialBad,untouched),Status::InvalidUtf8);
    assert(untouched[0]==0x1234 && untouched[1]==0x1234);
    const char32_t lateInvalid[]={U'A',0x110000};
    std::memset(out,0xa5,sizeof(out));
    Failed(EncodeSkeletalWireName<char32_t>(2,lateInvalid,out),Status::InvalidUnicode);
    for(auto byte:out)
    {
        assert(byte==0xa5);
    }
    const char32_t decomposed[]={U'e',0x301};
    const uint8_t decomposedBytes[]={0x65,0xcc,0x81};
    assert(EncodeSkeletalWireName<char32_t>(2,decomposed,out).ByteCount==3 && std::memcmp(out,decomposedBytes,3)==0);
    const char32_t composed[]={0xe9};
    assert(EncodeSkeletalWireName<char32_t>(2,composed,out).ByteCount==2 && out[0]==0xc3 && out[1]==0xa9);
    const char16_t high[]={0xd800}; const char16_t low[]={0xdc00}; const char16_t wrongPair[]={0xd800,u'A'};
    for(Span<const char16_t> value : {Span<const char16_t>(high),Span<const char16_t>(low),Span<const char16_t>(wrongPair)})
    {
        std::memset(out,0xa5,sizeof(out));
        Failed(EncodeSkeletalWireName<char16_t>(2,value,out),Status::InvalidUnicode);
        for(auto byte:out)
        {
            assert(byte==0xa5);
        }
    }
    const char32_t invalid32[]={0x110000}; const char32_t surrogate32[]={0xdfff};
    Failed(EncodeSkeletalWireName<char32_t>(2,invalid32,out),Status::InvalidUnicode);
    Failed(EncodeSkeletalWireName<char32_t>(2,surrogate32,out),Status::InvalidUnicode);
    Failed(MeasureSkeletalNameEncoding<char>(3,{ascii,4}),Status::UnsupportedVersion);
    Failed(MeasureSkeletalNameDecoding<char>(3,text),Status::UnsupportedVersion);
    Failed(MeasureSkeletalNameEncoding<char>(2,{nullptr,1}),Status::InvalidInput);
    Failed(MeasureSkeletalNameDecoding<char>(2,{nullptr,1}),Status::InvalidInput);
    if constexpr (sizeof(size_t)>4)
    {
        Failed(MeasureSkeletalNameEncoding<char>(2,{ascii,size_t{UINT32_MAX}+1}),Status::NameTooLong);
        Failed(MeasureSkeletalNameDecoding<char>(2,{text,size_t{UINT32_MAX}+1}),Status::NameTooLong);
    }
    std::memset(out,0xa5,sizeof(out));
    Failed(EncodeSkeletalWireName<char16_t>(2,utf16,{out,6}),Status::InsufficientStorage);
    for(auto byte:out)
    {
        assert(byte==0xa5);
    }
    char16_t small[]={0x1234,0x1234};
    Failed(DecodeSkeletalWireName<char16_t>(2,text,small),Status::InsufficientStorage);
    assert(small[0]==0x1234 && small[1]==0x1234);
    uint8_t alias[16]={'A','B','C','D'};
    Failed(EncodeSkeletalWireName<unsigned char>(2,{alias,4},alias),Status::OverlappingStorage);
    Failed(DecodeSkeletalWireName<char>(2,{alias,4},{reinterpret_cast<char*>(alias+2),4}),Status::OverlappingStorage);
    // 実際に使わないout末尾との重複も拒否する。
    alias[8]='Z';
    Failed(EncodeSkeletalWireName<unsigned char>(2,{alias+8,1},alias),Status::OverlappingStorage);
    assert(alias[0]=='A' && alias[1]=='B' && alias[2]=='C' && alias[3]=='D' && alias[8]=='Z');
    alignas(char16_t) uint8_t misaligned[8]={};
    Failed(DecodeSkeletalWireName<char16_t>(2,{alias,1},{reinterpret_cast<char16_t*>(misaligned+1),2}),Status::InvalidInput);
    Failed(MeasureSkeletalNameEncoding<char16_t>(2,{reinterpret_cast<const char16_t*>(misaligned+1),1}),Status::InvalidInput);
    Failed(EncodeSkeletalWireName<char>(2,{ascii,4},{out,std::numeric_limits<size_t>::max()}),Status::InvalidInput);
    const auto empty=EncodeSkeletalWireName<char>(2,{},{});
    assert(empty.Succeeded() && empty.ByteCount==0 && empty.CodeUnitCount==0);
    assert(DecodeSkeletalWireName<char16_t>(2,{},{}).Succeeded());
    const uint8_t table[]={'X',0xe9,0xaa,0xa8,0xf0,0x9f,0x90,0xba,'Y'};
    const auto view=ResolveSkeletalWireName(2,table,1,7);
    assert(view.Succeeded() && view.Bytes.data()==table+1 && view.Bytes.size()==7);
    assert(ResolveSkeletalWireName(2,table,sizeof(table),0).Succeeded());
    assert(ResolveSkeletalWireName(2,{},0,0).Succeeded());
    for(auto ref : {ResolveSkeletalWireName(2,table,UINT64_MAX,1),ResolveSkeletalWireName(2,table,8,2)})
    {
        assert(ref.Status==Status::ReferenceOutOfRange && ref.Bytes.empty());
    }
    assert(ResolveSkeletalWireName(2,table,1,UINT64_MAX).Status==Status::NameTooLong);
    assert(ResolveSkeletalWireName(2,table,2,2).Status==Status::InvalidUtf8);
    assert(ResolveSkeletalWireName(2,table,1,2).Status==Status::InvalidUtf8);
    assert(ResolveSkeletalWireName(1,table,1,7).Status==Status::InvalidAscii);
    assert(ResolveSkeletalWireName(2,{nullptr,1},0,0).Status==Status::InvalidInput);
    // 全Unicode scalar（NUL/サロゲートを除く）の保存列をPythonの標準UTF-8 oracleとhash照合する。
    uint64_t streamHash=14695981039346656037ull, byteCount=0;
    for(uint32_t codePoint=1;codePoint<=0x10ffff;++codePoint)
    {
        if(codePoint>=0xd800 && codePoint<=0xdfff)
        {
            continue;
        }
        const char32_t scalar=static_cast<char32_t>(codePoint);
        uint8_t encoded[4]={}; char32_t roundtrip[1]={}; char16_t utf16Roundtrip[2]={}; uint8_t encoded16[4]={};
        const auto written=EncodeSkeletalWireName<char32_t>(2,{&scalar,1},encoded);
        assert(written.Succeeded());
        const auto decoded=DecodeSkeletalWireName<char32_t>(2,{encoded,written.ByteCount},roundtrip);
        assert(decoded.Succeeded() && roundtrip[0]==scalar && decoded.CodeUnitCount==1);
        const auto d16=DecodeSkeletalWireName<char16_t>(2,{encoded,written.ByteCount},utf16Roundtrip);
        assert(d16.Succeeded());
        const auto e16=EncodeSkeletalWireName<char16_t>(2,{utf16Roundtrip,d16.CodeUnitCount},encoded16);
        assert(e16.Succeeded() && e16.ByteCount==written.ByteCount && std::memcmp(encoded,encoded16,written.ByteCount)==0);
        for(size_t index=0;index<written.ByteCount;++index)
        {
            streamHash=(streamHash^encoded[index])*1099511628211ull;
            ++byteCount;
        }
    }
    assert(streamHash==0x20a9b4125d4cb87ull && byteCount==4382591ull);
    std::cout << "CookedSkeletalNameCodecTest PASS: legacy_utf8_utf16_utf32_refs_bounds_atomicity_all_scalars\n";
    return 0;
}
