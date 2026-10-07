#include "CookBatchReport.h"
#include "AssetCookOutput.h"
#include "CookManagedTransactionController.h"
#include <algorithm>
#include <charconv>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace
    {
        bool Add(uint64_t& total, uint64_t value)
        {
            if (value > UINT64_MAX - total)
            {
                return false;
            }
            total += value;
            return true;
        }
        C::AnsiString Number(uint64_t n)
        {
            char b[32];
            const auto r = std::to_chars(b, b + sizeof(b), n);
            return C::AnsiString(C::AnsiStringView(b, size_t(r.ptr - b)));
        }
        C::AnsiString Limit(uint64_t value)
        {
            return value == UINT64_MAX ? C::AnsiString("null") : Number(value);
        }
        C::AnsiString Quote(const C::AnsiString& text)
        {
            C::AnsiString out = "\"";
            for (char c : text)
            {
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                }
                if (static_cast<unsigned char>(c) < 32)
                {
                    const char* hex = "0123456789abcdef";
                    const unsigned byte = static_cast<unsigned char>(c);
                    out += "\\u00";
                    out += hex[byte >> 4];
                    out += hex[byte & 15];
                }
                else
                {
                    out += c;
                }
            }
            out += '"';
            return out;
        }
        uint32_t Exceeded(const CookAssetMetrics& m, const CookAssetBudget& b)
        {
            return uint32_t(m.Triangles > b.MaxTriangles) + uint32_t(m.Joints > b.MaxJoints) +
                   uint32_t(m.TextureBytes > b.MaxTextureBytes) + uint32_t(m.CookedBytes > b.MaxCookedBytes);
        }
    } // namespace
    bool MergeCookOutputMetrics(CookAssetMetrics& total, const CookAssetMetrics& value)
    {
        auto candidate = total;
        if (!Add(candidate.Triangles, value.Triangles) || !Add(candidate.TextureBytes, value.TextureBytes) ||
            !Add(candidate.CookedBytes, value.CookedBytes))
        {
            return false;
        }
        candidate.Joints = std::max(candidate.Joints, value.Joints);
        total = candidate;
        return true;
    }
    bool CheckCookBatchBudgets(CookBatchReport& report, const CookAssetBudget& budget)
    {
        report.Totals = {};
        report.BudgetErrors = 0;
        for (const auto& row : report.Assets)
        {
            const auto& m = row.Metrics;
            if (!Add(report.Totals.Triangles, m.Triangles) || !Add(report.Totals.Joints, m.Joints) ||
                !Add(report.Totals.TextureBytes, m.TextureBytes) || !Add(report.Totals.CookedBytes, m.CookedBytes))
            {
                return false;
            }
            report.BudgetErrors += Exceeded(m, row.Budget);
        }
        report.BudgetErrors += Exceeded(report.Totals, budget);
        return report.BudgetErrors == 0 || report.bWarnBudget;
    }
    bool WriteCookBatchReport(CookBatchReport& report, C::AnsiString& error)
    {
        if (!report.bEnabled)
        {
            return true;
        }
        uint64_t skipped = 0, cooked = 0;
        for (const auto& a : report.Assets)
        {
            skipped += a.bProcessed && a.bSkipped;
            cooked += a.bProcessed && !a.bSkipped;
        }
        C::AnsiString json = "{\"version\":1,\"summary\":{\"cooked\":" + Number(cooked) +
                             ",\"skipped\":" + Number(skipped) + ",\"failed\":" + Number(report.bFailed) +
                             ",\"budget_errors\":" + Number(report.BudgetErrors) +
                             "},\"error\":" + Quote(report.Error) + ",\"assets\":[";
        C::AnsiString md = "# Cook report\n\n予算超過: " + Number(report.BudgetErrors) + "\n\n" + report.Error + "\n\n";
        for (size_t i = 0; i < report.Assets.size(); ++i)
        {
            const auto& a = report.Assets[i];
            const auto& m = a.Metrics;
            if (i)
            {
                json += ",";
            }
            json += "{\"logical_path\":" + Quote(a.LogicalPath) + ",\"kind\":" + Quote(a.Kind) + ",\"status\":\"" +
                    (!a.bProcessed ? C::AnsiString("not_completed")
                                   : (a.bSkipped ? C::AnsiString("skipped") : C::AnsiString("cooked"))) +
                    "\",\"source_bytes\":" + Number(a.SourceBytes) + ",\"cooked_bytes\":" + Number(m.CookedBytes) +
                    ",\"stats\":{\"triangles\":" + Number(m.Triangles) + ",\"joints\":" + Number(m.Joints) +
                    ",\"texture_bytes\":" + Number(m.TextureBytes) +
                    "},\"budget\":{\"ok\":" + (Exceeded(m, a.Budget) ? C::AnsiString("false") : C::AnsiString("true")) +
                    ",\"max_triangles\":" + Limit(a.Budget.MaxTriangles) +
                    ",\"max_joints\":" + Limit(a.Budget.MaxJoints) +
                    ",\"max_texture_bytes\":" + Limit(a.Budget.MaxTextureBytes) +
                    ",\"max_cooked_bytes\":" + Limit(a.Budget.MaxCookedBytes) + "}}";
            md += "- " + a.LogicalPath + ": " +
                  (!a.bProcessed ? C::AnsiString("not_completed")
                                 : (a.bSkipped ? C::AnsiString("skipped") : C::AnsiString("cooked"))) +
                  ", triangles=" + Number(m.Triangles) + ", joints=" + Number(m.Joints) +
                  ", texture_bytes=" + Number(m.TextureBytes) + ", cooked_bytes=" + Number(m.CookedBytes) + "\n";
        }
        json += "],\"totals\":{\"triangles\":" + Number(report.Totals.Triangles) +
                ",\"joints\":" + Number(report.Totals.Joints) +
                ",\"texture_bytes\":" + Number(report.Totals.TextureBytes) +
                ",\"cooked_bytes\":" + Number(report.Totals.CookedBytes) + "}}\n";
        // 既存reportを置換しない。nativeの排他的な新規directory/fileだけを作る。
#if defined(_WIN32)
        namespace N = Detail::ManagedStoreNative;
        namespace T = Detail::ManagedTransaction;
        N::Identity parent, reports, run;
        if (!N::Directory(report.RuntimeRoot.parent_path(), parent, error))
        {
            return false;
        }
        auto reportLeaf = report.RuntimeRoot.filename();
        reportLeaf += L".reports";
        const auto reportPath = report.RuntimeRoot.parent_path() / reportLeaf;
        std::error_code ec;
        const bool present = std::filesystem::exists(reportPath, ec);
        if (ec)
        {
            error = "cook_report: directory observation failed";
            return false;
        }
        if (present)
        {
            if (!N::Directory(reportPath, reports, error) || !N::Direct(parent, reports))
            {
                return false;
            }
        }
        else if (!T::NewDirectory(parent, reportLeaf, reports, error))
        {
            return false;
        }
        C::AnsiString token;
        if (!T::Token(token, error) || !T::NewDirectory(reports, T::Leaf(token), run, error))
        {
            return false;
        }
        T::File jsonFile, mdFile;
        if (!T::NewFile(run, L"cook_report.json", T::BytesOf(json), jsonFile, error) ||
            !T::NewFile(run, L"cook_report.md", T::BytesOf(md), mdFile, error))
        {
            return false;
        }
        report.ReportDirectory = reportPath / T::Leaf(token);
#else
        error = "cook_report: native publication requires Windows";
        return false;
#endif
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
