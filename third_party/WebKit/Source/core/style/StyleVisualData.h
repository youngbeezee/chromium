/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
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

#ifndef StyleVisualData_h
#define StyleVisualData_h

#include "core/CoreExport.h"
#include "core/style/ComputedStyleConstants.h"
#include "platform/LengthBox.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/RefCounted.h"

namespace blink {

// TODO(sashab): Move this into a private class on ComputedStyle, and remove
// all methods on it, merging them into copy/creation methods on ComputedStyle
// instead. Keep the allocation logic, only allocating a new object if needed.
class CORE_EXPORT StyleVisualData : public RefCounted<StyleVisualData> {
 public:
  static PassRefPtr<StyleVisualData> Create() {
    return AdoptRef(new StyleVisualData);
  }
  PassRefPtr<StyleVisualData> Copy() const {
    return AdoptRef(new StyleVisualData(*this));
  }
  ~StyleVisualData();

  bool operator==(const StyleVisualData& o) const {
    return clip == o.clip && has_auto_clip == o.has_auto_clip &&
           text_decoration == o.text_decoration && zoom_ == o.zoom_;
  }
  bool operator!=(const StyleVisualData& o) const { return !(*this == o); }

  LengthBox clip;
  bool has_auto_clip : 1;
  unsigned text_decoration : kTextDecorationBits;  // Text decorations defined
                                                   // *only* by this element.

  float zoom_;

 private:
  StyleVisualData();
  StyleVisualData(const StyleVisualData&);
};

}  // namespace blink

#endif  // StyleVisualData_h
