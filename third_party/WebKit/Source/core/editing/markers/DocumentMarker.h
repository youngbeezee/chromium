/*
 * This file is part of the DOM implementation for WebCore.
 *
 * Copyright (C) 2006 Apple Computer, Inc.
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

#ifndef DocumentMarker_h
#define DocumentMarker_h

#include "core/CoreExport.h"
#include "platform/graphics/Color.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/VectorTraits.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class DocumentMarkerDetails;

// A range of a node within a document that is "marked", such as the range of a
// misspelled word. It optionally includes a description that could be displayed
// in the user interface. It also optionally includes a flag specifying whether
// the match is active, which is ignored for all types other than type
// TextMatch.
class CORE_EXPORT DocumentMarker : public GarbageCollected<DocumentMarker> {
 public:
  enum MarkerTypeIndex {
    kSpellingMarkerIndex = 0,
    kGrammarMarkerIndex,
    kTextMatchMarkerIndex,
    kCompositionMarkerIndex,
    kMarkerTypeIndexesCount
  };

  enum MarkerType {
    kSpelling = 1 << kSpellingMarkerIndex,
    kGrammar = 1 << kGrammarMarkerIndex,
    kTextMatch = 1 << kTextMatchMarkerIndex,
    kComposition = 1 << kCompositionMarkerIndex,
  };

  class MarkerTypesIterator
      : public std::iterator<std::forward_iterator_tag, MarkerType> {
   public:
    explicit MarkerTypesIterator(unsigned marker_types)
        : remaining_types_(marker_types) {}
    MarkerTypesIterator(const MarkerTypesIterator& other) = default;

    bool operator==(const MarkerTypesIterator& other) {
      return remaining_types_ == other.remaining_types_;
    }
    bool operator!=(const MarkerTypesIterator& other) {
      return !operator==(other);
    }

    MarkerTypesIterator& operator++() {
      DCHECK(remaining_types_);
      // Turn off least significant 1-bit (from Hacker's Delight 2-1)
      // Example:
      // 7: 7 & 6 = 6
      // 6: 6 & 5 = 4
      // 4: 4 & 3 = 0
      remaining_types_ &= (remaining_types_ - 1);
      return *this;
    }

    MarkerType operator*() const {
      DCHECK(remaining_types_);
      // Isolate least significant 1-bit (from Hacker's Delight 2-1)
      // Example:
      // 7: 7 & -7 = 1
      // 6: 6 & -6 = 2
      // 4: 4 & -4 = 4
      return static_cast<MarkerType>(remaining_types_ &
                                     (~remaining_types_ + 1));
    }

   private:
    unsigned remaining_types_;
  };

  class MarkerTypes {
   public:
    // The constructor is intentionally implicit to allow conversion from the
    // bit-wise sum of above types
    MarkerTypes(unsigned mask) : mask_(mask) {}

    bool Contains(MarkerType type) const { return mask_ & type; }
    bool Intersects(const MarkerTypes& types) const {
      return (mask_ & types.mask_);
    }
    bool operator==(const MarkerTypes& other) const {
      return mask_ == other.mask_;
    }

    void Add(const MarkerTypes& types) { mask_ |= types.mask_; }
    void Remove(const MarkerTypes& types) { mask_ &= ~types.mask_; }

    MarkerTypesIterator begin() const { return MarkerTypesIterator(mask_); }
    MarkerTypesIterator end() const { return MarkerTypesIterator(0); }

   private:
    unsigned mask_;
  };

  class AllMarkers : public MarkerTypes {
   public:
    AllMarkers()
        : MarkerTypes(kSpelling | kGrammar | kTextMatch | kComposition) {}
  };

  class MisspellingMarkers : public MarkerTypes {
   public:
    MisspellingMarkers() : MarkerTypes(kSpelling | kGrammar) {}
  };

  enum class MatchStatus { kInactive, kActive };

  DocumentMarker(MarkerType,
                 unsigned start_offset,
                 unsigned end_offset,
                 const String& description);
  DocumentMarker(unsigned start_offset, unsigned end_offset, MatchStatus);
  DocumentMarker(unsigned start_offset,
                 unsigned end_offset,
                 Color underline_color,
                 bool thick,
                 Color background_color);

  DocumentMarker(const DocumentMarker&);

  MarkerType GetType() const { return type_; }
  unsigned StartOffset() const { return start_offset_; }
  unsigned EndOffset() const { return end_offset_; }

  const String& Description() const;
  bool IsActiveMatch() const;
  Color UnderlineColor() const;
  bool Thick() const;
  Color BackgroundColor() const;
  DocumentMarkerDetails* Details() const;

  void SetIsActiveMatch(bool);
  void ClearDetails() { details_.Clear(); }

  struct MarkerOffsets {
    unsigned start_offset;
    unsigned end_offset;
  };

  Optional<MarkerOffsets> ComputeOffsetsAfterShift(unsigned offset,
                                                   unsigned old_length,
                                                   unsigned new_length) const;

  // Offset modifications are done by DocumentMarkerController.
  // Other classes should not call following setters.
  void SetStartOffset(unsigned offset) { start_offset_ = offset; }
  void SetEndOffset(unsigned offset) { end_offset_ = offset; }
  void ShiftOffsets(int delta);

  DECLARE_TRACE();

 private:
  MarkerType type_;
  unsigned start_offset_;
  unsigned end_offset_;
  Member<DocumentMarkerDetails> details_;
};

using DocumentMarkerVector = HeapVector<Member<DocumentMarker>>;

inline DocumentMarkerDetails* DocumentMarker::Details() const {
  return details_.Get();
}

class DocumentMarkerDetails
    : public GarbageCollectedFinalized<DocumentMarkerDetails> {
 public:
  DocumentMarkerDetails() {}
  virtual ~DocumentMarkerDetails();
  virtual bool IsDescription() const { return false; }
  virtual bool IsTextMatch() const { return false; }
  virtual bool IsComposition() const { return false; }

  DEFINE_INLINE_VIRTUAL_TRACE() {}
};

}  // namespace blink

#endif  // DocumentMarker_h
