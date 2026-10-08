#pragma once
// v1の書式を保ちつつ、v2の複数packageを同じtransactionへ集約する。
#include "CookManagedTransactionController.h"
#include "CookOutputPlan.h"
#include "TextureAssetSetOutput.h"
#include "AssetCookOutput.h"
namespace NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
{
    inline bool SerializeManagedReferences(bool bLegacy,
                                           Core::Container::Span<const Core::Asset::AssetCookedReference> references,
                                           Text& out, Text& error)
    {
        if (bLegacy)
        {
            return Detail::SerializeLegacyTextureManifest(references, out, error);
        }
        std::string reason;
        if (!Detail::SerializeCookedManifestReferences(references, out, reason))
        {
            error = reason.c_str();
            return false;
        }
        return true;
    }
    inline bool InitializeCookReport(const CookManagedBootstrapRequest& request,
                                     Core::Container::Span<const CookPreparedPlan> plans, Text& error)
    {
        if (!request.Report)
        {
            return true;
        }
        if (!request.Budgets.empty() && request.Budgets.size() != plans.size())
        {
            return Fail(error, "budget_count");
        }
        request.Report->Assets.clear();
        for (size_t i = 0; i < plans.size(); ++i)
        {
            CookAssetReportRow row;
            row.LogicalPath = plans[i].Context.Request.LogicalPath;
            row.Kind = plans[i].Context.Request.Kind;
            if (!request.Budgets.empty())
            {
                row.Budget = request.Budgets[i];
            }
            for (const auto& file : plans[i].Context.Dependencies.Files)
            {
                if (file.Role == CookDependencyRole::Source && file.bPresent)
                {
                    row.SourceBytes = file.Size;
                }
            }
            request.Report->Assets.push_back(std::move(row));
        }
        return true;
    }
    inline bool AddCookReportOutputs(const CookManagedBootstrapRequest& request, size_t index,
                                     Core::Container::Span<const CookRecordedOutput> outputs, Text& error)
    {
        if (!request.Report)
        {
            return true;
        }
        if (index >= request.Report->Assets.size())
        {
            return Fail(error, "report_index");
        }
        request.Report->Assets[index].bProcessed = true;
        auto& metrics = request.Report->Assets[index].Metrics;
        metrics = {};
        for (const auto& output : outputs)
        {
            if (!MergeCookOutputMetrics(metrics, output.Package.Metrics))
            {
                return Fail(error, "report_overflow");
            }
        }
        return true;
    }
    inline bool CheckManagedBudgets(const CookManagedBootstrapRequest& request, Text& error)
    {
        return !request.Report || CheckCookBatchBudgets(*request.Report, request.TotalBudget) ||
               Fail(error, "budget_exceeded");
    }
#if defined(_WIN32)
    inline const TreeNode* FindWorkPackage(Core::Container::Span<const TreeNode> tree, View path)
    {
        for (const auto& node : tree)
        {
            if (!node.bDirectory && View(node.Relative.data(), node.Relative.size()) == path)
            {
                return &node;
            }
        }
        return nullptr;
    }
    inline bool ValidateWorkOutputs(Core::Container::Span<const TreeNode> tree,
                                    Core::Container::Span<const CookRecordedOutput> outputs, View manifest, Text& error)
    {
        if (outputs.empty())
        {
            return Fail(error, "empty_work_outputs");
        }
        for (const auto& node : tree)
        {
            bool known = !node.bDirectory && View(node.Relative.data(), node.Relative.size()) == manifest;
            for (const auto& output : outputs)
            {
                const auto& path = output.Reference.CookedPackage;
                if (!node.bDirectory && path == node.Relative)
                {
                    known = true;
                }
                if (node.bDirectory && path.size() > node.Relative.size() && path[node.Relative.size()] == '/' &&
                    std::memcmp(path.data(), node.Relative.data(), node.Relative.size()) == 0)
                {
                    known = true;
                }
            }
            if (!known)
            {
                return Fail(error, "unexpected_work_entry");
            }
        }
        for (const auto& output : outputs)
        {
            const auto* node = FindWorkPackage(tree, output.Reference.CookedPackage);
            if (!node || node->Image.Size != output.Package.Size ||
                node->Image.ContentHash != output.Package.ContentHash)
            {
                return Fail(error, "staged_package_changed");
            }
        }
        return true;
    }
#endif
} // namespace NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
