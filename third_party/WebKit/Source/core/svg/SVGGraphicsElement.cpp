/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 * Copyright (C) 2014 Google, Inc.
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
 */

#include "core/svg/SVGGraphicsElement.h"

#include "core/SVGNames.h"
#include "core/dom/StyleChangeReason.h"
#include "core/frame/FrameView.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/svg/LayoutSVGRoot.h"
#include "core/svg/SVGElementRareData.h"
#include "core/svg/SVGMatrixTearOff.h"
#include "core/svg/SVGRectTearOff.h"
#include "platform/transforms/AffineTransform.h"

namespace blink {

SVGGraphicsElement::SVGGraphicsElement(const QualifiedName& tag_name,
                                       Document& document,
                                       ConstructionType construction_type)
    : SVGElement(tag_name, document, construction_type),
      SVGTests(this),
      transform_(SVGAnimatedTransformList::Create(this,
                                                  SVGNames::transformAttr,
                                                  CSSPropertyTransform)) {
  AddToPropertyMap(transform_);
}

SVGGraphicsElement::~SVGGraphicsElement() {}

DEFINE_TRACE(SVGGraphicsElement) {
  visitor->Trace(transform_);
  SVGElement::Trace(visitor);
  SVGTests::Trace(visitor);
}

static bool IsViewportElement(const Element& element) {
  return (isSVGSVGElement(element) || isSVGSymbolElement(element) ||
          isSVGForeignObjectElement(element) || isSVGImageElement(element));
}

AffineTransform SVGGraphicsElement::ComputeCTM(
    SVGElement::CTMScope mode,
    SVGGraphicsElement::StyleUpdateStrategy style_update_strategy,
    const SVGGraphicsElement* ancestor) const {
  if (style_update_strategy == kAllowStyleUpdate)
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  AffineTransform ctm;
  bool done = false;

  for (const Element* current_element = this; current_element && !done;
       current_element = current_element->ParentOrShadowHostElement()) {
    if (!current_element->IsSVGElement())
      break;

    ctm = ToSVGElement(current_element)
              ->LocalCoordinateSpaceTransform()
              .Multiply(ctm);

    switch (mode) {
      case kNearestViewportScope:
        // Stop at the nearest viewport ancestor.
        done = current_element != this && IsViewportElement(*current_element);
        break;
      case kAncestorScope:
        // Stop at the designated ancestor.
        done = current_element == ancestor;
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  return ctm;
}

AffineTransform SVGGraphicsElement::GetCTM(
    StyleUpdateStrategy style_update_strategy) {
  return ComputeCTM(kNearestViewportScope, style_update_strategy);
}

AffineTransform SVGGraphicsElement::GetScreenCTM(
    StyleUpdateStrategy style_update_strategy) {
  if (style_update_strategy == kAllowStyleUpdate)
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  TransformationMatrix transform;
  if (LayoutObject* layout_object = this->GetLayoutObject()) {
    // Adjust for the zoom level factored into CSS coordinates (WK bug #96361).
    transform.Scale(1.0 / layout_object->StyleRef().EffectiveZoom());

    // Origin in the document. (This, together with the inverse-scale above,
    // performs the same operation as
    // Document::adjustFloatRectForScrollAndAbsoluteZoom, but in transformation
    // matrix form.)
    if (FrameView* view = GetDocument().View()) {
      LayoutRect visible_content_rect(view->VisibleContentRect());
      transform.Translate(-visible_content_rect.X(), -visible_content_rect.Y());
    }

    // Apply transforms from our ancestor coordinate space, including any
    // non-SVG ancestor transforms.
    transform.Multiply(layout_object->LocalToAbsoluteTransform());

    // At the SVG/HTML boundary (aka LayoutSVGRoot), we need to apply the
    // localToBorderBoxTransform to map an element from SVG viewport
    // coordinates to CSS box coordinates.
    if (layout_object->IsSVGRoot()) {
      transform.Multiply(
          ToLayoutSVGRoot(layout_object)->LocalToBorderBoxTransform());
    }
  }
  // Drop any potential non-affine parts, because we're not able to convey that
  // information further anyway until getScreenCTM returns a DOMMatrix (4x4
  // matrix.)
  return transform.ToAffineTransform();
}

SVGMatrixTearOff* SVGGraphicsElement::getCTMFromJavascript() {
  return SVGMatrixTearOff::Create(GetCTM());
}

SVGMatrixTearOff* SVGGraphicsElement::getScreenCTMFromJavascript() {
  return SVGMatrixTearOff::Create(GetScreenCTM());
}

void SVGGraphicsElement::CollectStyleForPresentationAttribute(
    const QualifiedName& name,
    const AtomicString& value,
    MutableStylePropertySet* style) {
  if (name == SVGNames::transformAttr) {
    AddPropertyToPresentationAttributeStyle(
        style, CSSPropertyTransform, transform_->CurrentValue()->CssValue());
    return;
  }
  SVGElement::CollectStyleForPresentationAttribute(name, value, style);
}

AffineTransform* SVGGraphicsElement::AnimateMotionTransform() {
  return EnsureSVGRareData()->AnimateMotionTransform();
}

void SVGGraphicsElement::SvgAttributeChanged(const QualifiedName& attr_name) {
  // Reattach so the isValid() check will be run again during layoutObject
  // creation.
  if (SVGTests::IsKnownAttribute(attr_name)) {
    SVGElement::InvalidationGuard invalidation_guard(this);
    LazyReattachIfAttached();
    return;
  }

  if (attr_name == SVGNames::transformAttr) {
    SVGElement::InvalidationGuard invalidation_guard(this);
    InvalidateSVGPresentationAttributeStyle();
    // TODO(fs): The InvalidationGuard will make sure all instances are
    // invalidated, but the style recalc will propagate to instances too. So
    // there is some redundant operations being performed here. Could we get
    // away with removing the InvalidationGuard?
    SetNeedsStyleRecalc(kLocalStyleChange,
                        StyleChangeReasonForTracing::FromAttribute(attr_name));
    if (LayoutObject* object = GetLayoutObject())
      MarkForLayoutAndParentResourceInvalidation(object);
    return;
  }

  SVGElement::SvgAttributeChanged(attr_name);
}

SVGElement* SVGGraphicsElement::nearestViewportElement() const {
  for (Element* current = ParentOrShadowHostElement(); current;
       current = current->ParentOrShadowHostElement()) {
    if (IsViewportElement(*current))
      return ToSVGElement(current);
  }

  return nullptr;
}

SVGElement* SVGGraphicsElement::farthestViewportElement() const {
  SVGElement* farthest = 0;
  for (Element* current = ParentOrShadowHostElement(); current;
       current = current->ParentOrShadowHostElement()) {
    if (IsViewportElement(*current))
      farthest = ToSVGElement(current);
  }
  return farthest;
}

FloatRect SVGGraphicsElement::GetBBox() {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME: Eventually we should support getBBox for detached elements.
  if (!GetLayoutObject())
    return FloatRect();

  return GetLayoutObject()->ObjectBoundingBox();
}

SVGRectTearOff* SVGGraphicsElement::getBBoxFromJavascript() {
  return SVGRectTearOff::Create(SVGRect::Create(GetBBox()), 0,
                                kPropertyIsNotAnimVal);
}

}  // namespace blink
