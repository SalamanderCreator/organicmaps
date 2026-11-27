#include "drape_frontend/drape_api_builder.hpp"
#include "drape_frontend/area_shape.hpp"
#include "drape_frontend/batcher_bucket.hpp"
#include "drape_frontend/colored_symbol_shape.hpp"
#include "drape_frontend/gui/gui_text.hpp"
#include "drape_frontend/line_shape.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/visual_params.hpp"

#include "drape/batcher.hpp"

#include "indexer/feature_decl.hpp"

#include "base/string_utils.hpp"

#include "geometry/robust_orientation.hpp"

#include "3party/earcut/earcut.hpp"

#include <array>

namespace
{
void BuildText(ref_ptr<dp::GraphicsContext> context, std::string const & str, dp::FontDecl const & font,
               m2::PointD const & position, m2::PointD const & center, ref_ptr<dp::TextureManager> textures,
               dp::Batcher & batcher, glsl::vec2 const & pixelOffset = glsl::vec2(0.0f, 0.0f),
               dp::Anchor anchor = dp::LeftTop)
{
  gui::StaticLabel::LabelResult result;
  gui::StaticLabel::CacheStaticText(str, "\n", anchor, font, textures, result);
  glsl::vec2 const pt = glsl::ToVec2(df::MapShape::ConvertToLocal(position, center, df::kShapeCoordScalar));
  for (gui::StaticLabel::Vertex & v : result.m_buffer)
  {
    v.m_position = glsl::vec3(pt, 0.0f);
    v.m_normal += pixelOffset;
  }

  dp::AttributeProvider provider(1 /* streamCount */, static_cast<uint32_t>(result.m_buffer.size()));
  provider.InitStream(0 /* streamIndex */, gui::StaticLabel::Vertex::GetBindingInfo(),
                      make_ref(result.m_buffer.data()));

  batcher.InsertListOfStrip(context, result.m_state, make_ref(&provider), dp::Batcher::VertexPerQuad);
}
}  // namespace

namespace df
{
void DrapeApiBuilder::BuildLines(ref_ptr<dp::GraphicsContext> context, DrapeApi::TLines & lines,
                                 ref_ptr<dp::TextureManager> textures,
                                 std::vector<drape_ptr<DrapeApiRenderProperty>> & properties)
{
  properties.reserve(lines.size());

  uint32_t constexpr kMaxSize = 5000;
  uint32_t constexpr kFontSize = 14;
  FeatureID fakeFeature;

  for (auto & line : lines)
  {
    std::string const & id = line.first;

    DrapeApiLineData & data = line.second;
    base::Unique(data.m_points,
                 [](m2::PointD const & p1, m2::PointD const & p2) { return p1.EqualDxDy(p2, kMwmPointAccuracy); });

    m2::RectD rect;
    for (auto const & p : data.m_points)
      rect.Add(p);
    for (auto const & part : {&data.m_polylines, &data.m_fillRings})
      for (auto const & points : *part)
        for (auto const & p : points)
          rect.Add(p);

    dp::Batcher batcher(kMaxSize, kMaxSize);
    batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
    auto property = make_unique_dp<DrapeApiRenderProperty>();
    property->m_center = rect.Center();
    {
      dp::SessionGuard guard(context, batcher,
                             [&property, &id](dp::RenderState const & state, drape_ptr<dp::RenderBucket> && b)
      {
        property->m_id = id;
        property->m_buckets.emplace_back(state, std::move(b));
      });

      if (data.m_fill && data.m_points.size() > 2)
        data.m_fillRings.push_back(data.m_points);
      std::vector<m2::PointD> triangles;
      for (auto const & ring : data.m_fillRings)
      {
        if (ring.size() < 3)
          continue;
        std::vector<std::vector<std::array<double, 2>>> rings(1);
        for (auto const & p : ring)
          rings.front().push_back({p.x, p.y});
        // Back faces are culled, so all triangles must be clockwise, which earcut does not promise.
        auto const indices = mapbox::earcut<uint32_t>(rings);
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
          auto const & a = ring[indices[i]];
          auto const & b = ring[indices[i + 1]];
          auto const & c = ring[indices[i + 2]];
          double const s = m2::robust::OrientedS(a, b, c);
          if (s < 0)
            triangles.insert(triangles.end(), {a, b, c});
          else if (s > 0)
            triangles.insert(triangles.end(), {a, c, b});
        }
      }
      if (!triangles.empty())
      {
        AreaViewParams avp;
        avp.m_tileCenter = property->m_center;
        avp.m_depthTestEnabled = false;
        avp.m_minVisibleScale = 1;
        avp.m_color = data.m_fillColor;
        AreaShape(std::move(triangles), {}, avp).Draw(context, make_ref(&batcher), textures);
      }

      auto const drawLine = [&](std::vector<m2::PointD> const & points)
      {
        m2::SharedSpline spline(points);
        LineViewParams lvp;
        lvp.m_tileCenter = property->m_center;
        lvp.m_depthTestEnabled = false;
        lvp.m_minVisibleScale = 1;
        lvp.m_cap = dp::RoundCap;
        lvp.m_color = data.m_color;
        lvp.m_width = data.m_width;
        lvp.m_join = dp::RoundJoin;
        if (!data.m_pattern.empty())
        {
          double const visualScale = VisualParams::Instance().GetVisualScale();
          for (double const d : data.m_pattern)
            lvp.m_pattern.push_back(dp::PatternFloat2Pixel(d * visualScale));
          lvp.m_baseGtoPScale = data.m_baseGtoPScale;
          lvp.m_cap = dp::ButtCap;
        }
        LineShape(spline, lvp).Draw(context, make_ref(&batcher), textures);
      };
      for (auto const & polyline : data.m_polylines)
        if (polyline.size() > 1)
          drawLine(polyline);

      if (data.m_points.size() > 1 && !data.m_noLine)
        drawLine(data.m_points);

      if (data.m_showPoints)
      {
        ColoredSymbolViewParams cvp;
        cvp.m_tileCenter = property->m_center;
        cvp.m_depthTestEnabled = false;
        cvp.m_minVisibleScale = 1;
        cvp.m_shape = ColoredSymbolViewParams::Shape::Circle;
        cvp.m_color = data.m_color;
        cvp.m_radiusInPixels = data.m_width * 2.0f;
        for (m2::PointD const & pt : data.m_points)
        {
          ColoredSymbolShape(m2::PointD(pt), cvp, TileKey(), 0 /* textIndex */, false /* need overlay */)
              .Draw(context, make_ref(&batcher), textures);
        }
      }

      if (!data.m_label.empty() && !data.m_points.empty())
      {
        // A halo keeps the label readable on colored map features, and a gap keeps it off the point symbol.
        float const vs = static_cast<float>(VisualParams::Instance().GetVisualScale());
        dp::FontDecl const font(data.m_color, data.m_labelSize,
                                data.m_labelHalo ? dp::Color::White() : dp::Color::Transparent());
        if (data.m_labelCentered)
        {
          BuildText(context, data.m_label, font, data.m_points.front(), property->m_center, textures, batcher,
                    glsl::vec2(0.0f, 0.0f), dp::Center);
        }
        else
        {
          BuildText(context, data.m_label, font, data.m_points.front(), property->m_center, textures, batcher,
                    glsl::vec2(14.0f * vs, 4.0f * vs));
        }
      }

      if (data.m_markPoints || data.m_showId)
      {
        dp::FontDecl font(data.m_color, kFontSize);
        size_t index = 0;
        for (m2::PointD const & pt : data.m_points)
        {
          if (index > 0 && !data.m_markPoints)
            break;

          std::string s;
          if (data.m_markPoints)
            s = strings::to_string(index) + ((data.m_showId && index == 0) ? (" (" + id + ")") : "");
          else
            s = id;

          BuildText(context, s, font, pt, property->m_center, textures, batcher);
          index++;
        }
      }
    }

    if (!property->m_buckets.empty())
      properties.push_back(std::move(property));
  }
}
}  // namespace df
