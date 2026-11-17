// 空の大気モデルと空由来 IBL の放射輝度の源を再利用する。
#include "Rendering/SkyAtmosphereSourceCache.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float ClampSkyRadianceForFp16(float value)
        {
            constexpr float kFp16SafeMax = 65504.0f * 0.9f;
            return std::isfinite(value) ? std::clamp(value, 0.0f, kFp16SafeMax) : 0.0f;
        }

        Math::Vector3 SkyDirectionFromEquirectangular(uint32_t x,
                                                       uint32_t y,
                                                       uint32_t width,
                                                       uint32_t height)
        {
            constexpr float kPi = 3.14159265358979323846f;
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            const float longitude = (u - 0.5f) * 2.0f * kPi;
            const float latitude = (v - 0.5f) * kPi;
            const float horizontal = std::cos(latitude);
            return Math::Vector3(horizontal * std::cos(longitude),
                                 -std::sin(latitude),
                                 horizontal * std::sin(longitude));
        }
    }

    const SkyAtmosphereModel* SkyAtmosphereSourceCache::AcquireModel(const SkyAtmosphereParameters& parameters)
    {
        const SkyAtmosphereParameters clearKey = MakeSkyAtmosphereClearKey(parameters);
        if (!clearKey.bEnabled)
        {
            return nullptr;
        }

        if (m_Model && AreSkyAtmosphereClearKeysEqual(m_ModelParameters, clearKey))
        {
            return m_Model.get();
        }

        Container::TUniquePtr<SkyAtmosphereModel> created =
            Container::MakeUnique<SkyAtmosphereModel>(clearKey, m_Model.get());
        created->BuildSkyViewTable();
        m_Model = std::move(created);
        m_ModelParameters = clearKey;
        ++m_ModelBuildCount;
        m_bRadianceSourceBuilt = false;
        m_bRadianceSourceValid = false;
        m_RadianceSource.clear();
        return m_Model.get();
    }

    const Container::VariableArray<float>* SkyAtmosphereSourceCache::AcquireRadianceSource(
        const SkyAtmosphereParameters& parameters, uint32_t width, uint32_t height)
    {
        const SkyAtmosphereModel* model = AcquireModel(parameters);
        if (model == nullptr)
        {
            return nullptr;
        }

        if (width == 0u || height == 0u ||
            static_cast<size_t>(width) > std::numeric_limits<size_t>::max() /
                                         static_cast<size_t>(height))
        {
            return nullptr;
        }

        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (pixelCount > std::numeric_limits<size_t>::max() / 4u)
        {
            return nullptr;
        }

        if (m_bRadianceSourceBuilt && m_RadianceSourceWidth == width && m_RadianceSourceHeight == height)
        {
            return m_bRadianceSourceValid ? &m_RadianceSource : nullptr;
        }

        m_RadianceSource.clear();
        m_RadianceSourceWidth = width;
        m_RadianceSourceHeight = height;
        m_bRadianceSourceBuilt = true;
        m_bRadianceSourceValid = false;
        try
        {
            m_RadianceSource.resize(pixelCount * 4u, 0.0f);
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
        catch (const std::length_error&)
        {
            return nullptr;
        }

        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const SkyRadianceSample sample = model->SampleSkyView(
                    SkyDirectionFromEquirectangular(x, y, width, height));
                if (!sample.bValid || !std::isfinite(sample.Radiance.x) ||
                    !std::isfinite(sample.Radiance.y) ||
                    !std::isfinite(sample.Radiance.z))
                {
                    m_RadianceSource.clear();
                    return nullptr;
                }

                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                m_RadianceSource[offset + 0u] = ClampSkyRadianceForFp16(sample.Radiance.x);
                m_RadianceSource[offset + 1u] = ClampSkyRadianceForFp16(sample.Radiance.y);
                m_RadianceSource[offset + 2u] = ClampSkyRadianceForFp16(sample.Radiance.z);
                m_RadianceSource[offset + 3u] = 1.0f;
            }
        }

        m_bRadianceSourceValid = true;
        return &m_RadianceSource;
    }

    const Container::VariableArray<float>* SkyAtmosphereSourceCache::AcquireUnitOvercastRadianceSource(
        const SkyAtmosphereParameters& parameters, uint32_t width, uint32_t height)
    {
        const SkyAtmosphereParameters sanitized = SanitizeSkyAtmosphereParameters(parameters);
        const SkyAtmosphereModel* model = AcquireModel(sanitized);
        if (model == nullptr)
        {
            return nullptr;
        }

        if (width == 0u || height == 0u ||
            static_cast<size_t>(width) > std::numeric_limits<size_t>::max() /
                                         static_cast<size_t>(height))
        {
            return nullptr;
        }
        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (pixelCount > std::numeric_limits<size_t>::max() / 4u)
        {
            return nullptr;
        }

        if (m_bUnitOvercastSourceBuilt && m_UnitOvercastSourceWidth == width &&
            m_UnitOvercastSourceHeight == height &&
            m_UnitOvercastParameters.PlanetRadiusMeters == sanitized.PlanetRadiusMeters &&
            m_UnitOvercastParameters.GroundAltitudeMeters == sanitized.GroundAltitudeMeters &&
            m_UnitOvercastParameters.GroundAlbedo.x == sanitized.GroundAlbedo.x &&
            m_UnitOvercastParameters.GroundAlbedo.y == sanitized.GroundAlbedo.y &&
            m_UnitOvercastParameters.GroundAlbedo.z == sanitized.GroundAlbedo.z &&
            m_UnitOvercastParameters.OvercastZenithToHorizonRatio == sanitized.OvercastZenithToHorizonRatio)
        {
            return m_bUnitOvercastSourceValid ? &m_UnitOvercastRadianceSource : nullptr;
        }

        m_UnitOvercastRadianceSource.clear();
        m_UnitOvercastParameters = sanitized;
        m_UnitOvercastSourceWidth = width;
        m_UnitOvercastSourceHeight = height;
        m_bUnitOvercastSourceBuilt = true;
        m_bUnitOvercastSourceValid = false;
        try
        {
            m_UnitOvercastRadianceSource.resize(pixelCount * 4u, 0.0f);
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
        catch (const std::length_error&)
        {
            return nullptr;
        }

        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const SkyRadianceSample sample = model->SampleUnitOvercastSkyView(
                    SkyDirectionFromEquirectangular(x, y, width, height),
                    sanitized.OvercastZenithToHorizonRatio);
                if (!sample.bValid || !std::isfinite(sample.Radiance.x) ||
                    !std::isfinite(sample.Radiance.y) || !std::isfinite(sample.Radiance.z))
                {
                    m_UnitOvercastRadianceSource.clear();
                    return nullptr;
                }

                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                m_UnitOvercastRadianceSource[offset + 0u] = ClampSkyRadianceForFp16(sample.Radiance.x);
                m_UnitOvercastRadianceSource[offset + 1u] = ClampSkyRadianceForFp16(sample.Radiance.y);
                m_UnitOvercastRadianceSource[offset + 2u] = ClampSkyRadianceForFp16(sample.Radiance.z);
                m_UnitOvercastRadianceSource[offset + 3u] = 1.0f;
            }
        }

        m_bUnitOvercastSourceValid = true;
        ++m_UnitOvercastBuildCount;
        return &m_UnitOvercastRadianceSource;
    }

    void SkyAtmosphereSourceCache::Reset()
    {
        m_Model.reset();
        m_ModelParameters = SkyAtmosphereParameters{};
        m_RadianceSource.clear();
        m_RadianceSourceWidth = 0u;
        m_RadianceSourceHeight = 0u;
        m_bRadianceSourceBuilt = false;
        m_bRadianceSourceValid = false;
        m_UnitOvercastParameters = SkyAtmosphereParameters{};
        m_UnitOvercastRadianceSource.clear();
        m_UnitOvercastSourceWidth = 0u;
        m_UnitOvercastSourceHeight = 0u;
        m_bUnitOvercastSourceBuilt = false;
        m_bUnitOvercastSourceValid = false;
    }
}
