/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2006, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (c) 2007, 2008, 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "platform/fonts/Font.h"

#include "platform/LayoutTestSupport.h"
#include "platform/LayoutUnit.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/fonts/CharacterRange.h"
#include "platform/fonts/FontCache.h"
#include "platform/fonts/FontFallbackIterator.h"
#include "platform/fonts/FontFallbackList.h"
#include "platform/fonts/SimpleFontData.h"
#include "platform/fonts/shaping/CachingWordShaper.h"
#include "platform/fonts/shaping/ShapeResultBloberizer.h"
#include "platform/geometry/FloatRect.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintFlags.h"
#include "platform/text/BidiResolver.h"
#include "platform/text/Character.h"
#include "platform/text/TextRun.h"
#include "platform/text/TextRunIterator.h"
#include "platform/transforms/AffineTransform.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/CharacterNames.h"
#include "platform/wtf/text/Unicode.h"
#include "third_party/skia/include/core/SkTextBlob.h"

namespace blink {

Font::Font() : can_shape_word_by_word_(0), shape_word_by_word_computed_(0) {}

Font::Font(const FontDescription& fd)
    : font_description_(fd),
      can_shape_word_by_word_(0),
      shape_word_by_word_computed_(0) {}

Font::Font(const Font& other)
    : font_description_(other.font_description_),
      font_fallback_list_(other.font_fallback_list_),
      // TODO(yosin): We should have a comment the reason why don't we copy
      // |m_canShapeWordByWord| and |m_shapeWordByWordComputed| from |other|,
      // since |operator=()| copies them from |other|.
      can_shape_word_by_word_(0),
      shape_word_by_word_computed_(0) {}

Font& Font::operator=(const Font& other) {
  font_description_ = other.font_description_;
  font_fallback_list_ = other.font_fallback_list_;
  can_shape_word_by_word_ = other.can_shape_word_by_word_;
  shape_word_by_word_computed_ = other.shape_word_by_word_computed_;
  return *this;
}

bool Font::operator==(const Font& other) const {
  FontSelector* first =
      font_fallback_list_ ? font_fallback_list_->GetFontSelector() : 0;
  FontSelector* second = other.font_fallback_list_
                             ? other.font_fallback_list_->GetFontSelector()
                             : 0;

  return first == second && font_description_ == other.font_description_ &&
         (font_fallback_list_
              ? font_fallback_list_->FontSelectorVersion()
              : 0) == (other.font_fallback_list_
                           ? other.font_fallback_list_->FontSelectorVersion()
                           : 0) &&
         (font_fallback_list_ ? font_fallback_list_->Generation() : 0) ==
             (other.font_fallback_list_
                  ? other.font_fallback_list_->Generation()
                  : 0);
}

void Font::Update(FontSelector* font_selector) const {
  // FIXME: It is pretty crazy that we are willing to just poke into a RefPtr,
  // but it ends up being reasonably safe (because inherited fonts in the render
  // tree pick up the new style anyway. Other copies are transient, e.g., the
  // state in the GraphicsContext, and won't stick around long enough to get you
  // in trouble). Still, this is pretty disgusting, and could eventually be
  // rectified by using RefPtrs for Fonts themselves.
  if (!font_fallback_list_)
    font_fallback_list_ = FontFallbackList::Create();
  font_fallback_list_->Invalidate(font_selector);
}

namespace {

void DrawBlobs(PaintCanvas* canvas,
               const PaintFlags& flags,
               const ShapeResultBloberizer::BlobBuffer& blobs,
               const FloatPoint& point) {
  for (const auto& blob_info : blobs) {
    DCHECK(blob_info.blob);
    PaintCanvasAutoRestore auto_restore(canvas, false);
    if (blob_info.rotation ==
        ShapeResultBloberizer::BlobRotation::kCCWRotation) {
      canvas->save();

      SkMatrix m;
      m.setSinCos(-1, 0, point.X(), point.Y());
      canvas->concat(m);
    }

    canvas->drawTextBlob(blob_info.blob, point.X(), point.Y(), flags);
  }
}

}  // anonymous ns

bool Font::DrawText(PaintCanvas* canvas,
                    const TextRunPaintInfo& run_info,
                    const FloatPoint& point,
                    float device_scale_factor,
                    const PaintFlags& flags) const {
  // Don't draw anything while we are using custom fonts that are in the process
  // of loading.
  if (ShouldSkipDrawing())
    return false;

  ShapeResultBloberizer bloberizer(*this, device_scale_factor);
  CachingWordShaper word_shaper(*this);
  ShapeResultBuffer buffer;
  word_shaper.FillResultBuffer(run_info, &buffer);
  bloberizer.FillGlyphs(run_info, buffer);
  DrawBlobs(canvas, flags, bloberizer.Blobs(), point);
  return true;
}

bool Font::DrawText(PaintCanvas* canvas,
                    const TextFragmentPaintInfo& text_info,
                    const FloatPoint& point,
                    float device_scale_factor,
                    const PaintFlags& flags) const {
  // Don't draw anything while we are using custom fonts that are in the process
  // of loading.
  if (ShouldSkipDrawing())
    return false;

  ShapeResultBloberizer bloberizer(*this, device_scale_factor);
  bloberizer.FillGlyphs(text_info.text, text_info.from, text_info.to,
                        text_info.shape_result);
  DrawBlobs(canvas, flags, bloberizer.Blobs(), point);
  return true;
}

bool Font::DrawBidiText(PaintCanvas* canvas,
                        const TextRunPaintInfo& run_info,
                        const FloatPoint& point,
                        CustomFontNotReadyAction custom_font_not_ready_action,
                        float device_scale_factor,
                        const PaintFlags& flags) const {
  // Don't draw anything while we are using custom fonts that are in the process
  // of loading, except if the 'force' argument is set to true (in which case it
  // will use a fallback font).
  if (ShouldSkipDrawing() &&
      custom_font_not_ready_action == kDoNotPaintIfFontNotReady)
    return false;

  // sub-run painting is not supported for Bidi text.
  const TextRun& run = run_info.run;
  DCHECK_EQ(run_info.from, 0u);
  DCHECK_EQ(run_info.to, run.length());
  BidiResolver<TextRunIterator, BidiCharacterRun> bidi_resolver;
  bidi_resolver.SetStatus(
      BidiStatus(run.Direction(), run.DirectionalOverride()));
  bidi_resolver.SetPositionIgnoringNestedIsolates(TextRunIterator(&run, 0));

  // FIXME: This ownership should be reversed. We should pass BidiRunList
  // to BidiResolver in createBidiRunsForLine.
  BidiRunList<BidiCharacterRun>& bidi_runs = bidi_resolver.Runs();
  bidi_resolver.CreateBidiRunsForLine(TextRunIterator(&run, run.length()));
  if (!bidi_runs.RunCount())
    return true;

  FloatPoint curr_point = point;
  BidiCharacterRun* bidi_run = bidi_runs.FirstRun();
  CachingWordShaper word_shaper(*this);
  while (bidi_run) {
    TextRun subrun =
        run.SubRun(bidi_run->Start(), bidi_run->Stop() - bidi_run->Start());
    bool is_rtl = bidi_run->Level() % 2;
    subrun.SetDirection(is_rtl ? TextDirection::kRtl : TextDirection::kLtr);
    subrun.SetDirectionalOverride(bidi_run->DirOverride(false));

    TextRunPaintInfo subrun_info(subrun);
    subrun_info.bounds = run_info.bounds;

    ShapeResultBloberizer bloberizer(*this, device_scale_factor);
    ShapeResultBuffer buffer;
    word_shaper.FillResultBuffer(subrun_info, &buffer);
    float run_width = bloberizer.FillGlyphs(subrun_info, buffer);
    DrawBlobs(canvas, flags, bloberizer.Blobs(), curr_point);

    bidi_run = bidi_run->Next();
    curr_point.Move(run_width, 0);
  }

  bidi_runs.DeleteRuns();
  return true;
}

void Font::DrawEmphasisMarks(PaintCanvas* canvas,
                             const TextRunPaintInfo& run_info,
                             const AtomicString& mark,
                             const FloatPoint& point,
                             float device_scale_factor,
                             const PaintFlags& flags) const {
  if (ShouldSkipDrawing())
    return;

  FontCachePurgePreventer purge_preventer;

  const auto emphasis_glyph_data = GetEmphasisMarkGlyphData(mark);
  if (!emphasis_glyph_data.font_data)
    return;

  ShapeResultBloberizer bloberizer(*this, device_scale_factor);
  CachingWordShaper word_shaper(*this);
  ShapeResultBuffer buffer;
  word_shaper.FillResultBuffer(run_info, &buffer);
  bloberizer.FillTextEmphasisGlyphs(run_info, emphasis_glyph_data, buffer);
  DrawBlobs(canvas, flags, bloberizer.Blobs(), point);
}

void Font::DrawEmphasisMarks(PaintCanvas* canvas,
                             const TextFragmentPaintInfo& text_info,
                             const AtomicString& mark,
                             const FloatPoint& point,
                             float device_scale_factor,
                             const PaintFlags& flags) const {
  if (ShouldSkipDrawing())
    return;

  FontCachePurgePreventer purge_preventer;
  const auto emphasis_glyph_data = GetEmphasisMarkGlyphData(mark);
  if (!emphasis_glyph_data.font_data)
    return;

  ShapeResultBloberizer bloberizer(*this, device_scale_factor);
  bloberizer.FillTextEmphasisGlyphs(
      text_info.text, text_info.direction, text_info.from, text_info.to,
      emphasis_glyph_data, text_info.shape_result);
  DrawBlobs(canvas, flags, bloberizer.Blobs(), point);
}

float Font::Width(const TextRun& run,
                  HashSet<const SimpleFontData*>* fallback_fonts,
                  FloatRect* glyph_bounds) const {
  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);
  return shaper.Width(run, fallback_fonts, glyph_bounds);
}

static int GetInterceptsFromBlobs(
    const ShapeResultBloberizer::BlobBuffer& blobs,
    const SkPaint& paint,
    const std::tuple<float, float>& bounds,
    SkScalar* intercepts_buffer) {
  SkScalar bounds_array[2] = {std::get<0>(bounds), std::get<1>(bounds)};

  int num_intervals = 0;
  for (const auto& blob_info : blobs) {
    DCHECK(blob_info.blob);

    // ShapeResultBloberizer splits for a new blob rotation, but does not split
    // for a change in font. A TextBlob can contain runs with differing fonts
    // and the getTextBlobIntercepts method handles multiple fonts for us. For
    // upright in vertical blobs we currently have to bail, see crbug.com/655154
    if (blob_info.rotation == ShapeResultBloberizer::BlobRotation::kCCWRotation)
      continue;

    SkScalar* offset_intercepts_buffer = nullptr;
    if (intercepts_buffer)
      offset_intercepts_buffer = &intercepts_buffer[num_intervals];
    num_intervals += paint.getTextBlobIntercepts(
        blob_info.blob.get(), bounds_array, offset_intercepts_buffer);
  }
  return num_intervals;
}

void Font::GetTextIntercepts(const TextRunPaintInfo& run_info,
                             float device_scale_factor,
                             const PaintFlags& flags,
                             const std::tuple<float, float>& bounds,
                             Vector<TextIntercept>& intercepts) const {
  if (ShouldSkipDrawing())
    return;

  ShapeResultBloberizer bloberizer(
      *this, device_scale_factor, ShapeResultBloberizer::Type::kTextIntercepts);

  CachingWordShaper word_shaper(*this);
  ShapeResultBuffer buffer;
  word_shaper.FillResultBuffer(run_info, &buffer);
  bloberizer.FillGlyphs(run_info, buffer);

  const auto& blobs = bloberizer.Blobs();

  // Get the number of intervals, without copying the actual values by
  // specifying nullptr for the buffer, following the Skia allocation model for
  // retrieving text intercepts.
  SkPaint paint(ToSkPaint(flags));
  int num_intervals = GetInterceptsFromBlobs(blobs, paint, bounds, nullptr);
  if (!num_intervals)
    return;
  DCHECK_EQ(num_intervals % 2, 0);
  intercepts.resize(num_intervals / 2);

  GetInterceptsFromBlobs(blobs, paint, bounds,
                         reinterpret_cast<SkScalar*>(intercepts.data()));
}

static inline FloatRect PixelSnappedSelectionRect(FloatRect rect) {
  // Using roundf() rather than ceilf() for the right edge as a compromise to
  // ensure correct caret positioning.
  float rounded_x = roundf(rect.X());
  return FloatRect(rounded_x, rect.Y(), roundf(rect.MaxX() - rounded_x),
                   rect.Height());
}

FloatRect Font::SelectionRectForText(const TextRun& run,
                                     const FloatPoint& point,
                                     int height,
                                     int from,
                                     int to,
                                     bool account_for_glyph_bounds) const {
  to = (to == -1 ? run.length() : to);

  FontCachePurgePreventer purge_preventer;

  CachingWordShaper shaper(*this);
  CharacterRange range = shaper.GetCharacterRange(run, from, to);

  return PixelSnappedSelectionRect(
      FloatRect(point.X() + range.start, point.Y(), range.Width(), height));
}

int Font::OffsetForPosition(const TextRun& run,
                            float x_float,
                            bool include_partial_glyphs) const {
  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);
  return shaper.OffsetForPosition(run, x_float, include_partial_glyphs);
}

ShapeCache* Font::GetShapeCache() const {
  return font_fallback_list_->GetShapeCache(font_description_);
}

bool Font::CanShapeWordByWord() const {
  if (!shape_word_by_word_computed_) {
    can_shape_word_by_word_ = ComputeCanShapeWordByWord();
    shape_word_by_word_computed_ = true;
  }
  return can_shape_word_by_word_;
};

bool Font::ComputeCanShapeWordByWord() const {
  if (!GetFontDescription().GetTypesettingFeatures())
    return true;

  if (!PrimaryFont())
    return false;

  const FontPlatformData& platform_data = PrimaryFont()->PlatformData();
  TypesettingFeatures features = GetFontDescription().GetTypesettingFeatures();
  return !platform_data.HasSpaceInLigaturesOrKerning(features);
};

void Font::WillUseFontData(const String& text) const {
  const FontFamily& family = GetFontDescription().Family();
  if (font_fallback_list_ && font_fallback_list_->GetFontSelector() &&
      !family.FamilyIsEmpty())
    font_fallback_list_->GetFontSelector()->WillUseFontData(
        GetFontDescription(), family.Family(), text);
}

PassRefPtr<FontFallbackIterator> Font::CreateFontFallbackIterator(
    FontFallbackPriority fallback_priority) const {
  return FontFallbackIterator::Create(font_description_, font_fallback_list_,
                                      fallback_priority);
}

GlyphData Font::GetEmphasisMarkGlyphData(const AtomicString& mark) const {
  if (mark.IsEmpty())
    return GlyphData();

  TextRun emphasis_mark_run(mark, mark.length());
  return CachingWordShaper(*this).EmphasisMarkGlyphData(emphasis_mark_run);
}

int Font::EmphasisMarkAscent(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Ascent();
}

int Font::EmphasisMarkDescent(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Descent();
}

int Font::EmphasisMarkHeight(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Height();
}

CharacterRange Font::GetCharacterRange(const TextRun& run,
                                       unsigned from,
                                       unsigned to) const {
  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);
  return shaper.GetCharacterRange(run, from, to);
}

Vector<CharacterRange> Font::IndividualCharacterRanges(
    const TextRun& run) const {
  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);
  auto ranges = shaper.IndividualCharacterRanges(run);
  // The shaper should return ranges.size == run.length but on some platforms
  // (OSX10.9.5) we are seeing cases in the upper end of the unicode range
  // where this is not true (see: crbug.com/620952). To catch these cases on
  // more popular platforms, and to protect users, we are using a CHECK here.
  CHECK_EQ(ranges.size(), run.length());
  return ranges;
}

bool Font::LoadingCustomFonts() const {
  return font_fallback_list_ && font_fallback_list_->LoadingCustomFonts();
}

bool Font::IsFallbackValid() const {
  return !font_fallback_list_ || font_fallback_list_->IsValid();
}

}  // namespace blink
