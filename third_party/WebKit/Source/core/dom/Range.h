/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Gunnstein Lye (gunnstein@netcom.no)
 * (C) 2000 Frederik Holljen (frederik.holljen@hig.no)
 * (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
 * reserved.
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

#ifndef Range_h
#define Range_h

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "core/CoreExport.h"
#include "core/dom/RangeBoundaryPoint.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/IntRect.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Forward.h"

namespace blink {

class ClientRect;
class ClientRectList;
class ContainerNode;
class Document;
class DocumentFragment;
class ExceptionState;
class FloatQuad;
class Node;
class NodeWithIndex;
class Text;

class CORE_EXPORT Range final : public GarbageCollected<Range>,
                                public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static Range* Create(Document&);
  static Range* Create(Document&,
                       Node* start_container,
                       unsigned start_offset,
                       Node* end_container,
                       unsigned end_offset);
  static Range* Create(Document&, const Position&, const Position&);
  static Range* CreateAdjustedToTreeScope(const TreeScope&, const Position&);

  void Dispose();

  Document& OwnerDocument() const {
    DCHECK(owner_document_);
    return *owner_document_.Get();
  }
  Node* startContainer() const { return &start_.Container(); }
  unsigned startOffset() const { return start_.Offset(); }
  Node* endContainer() const { return &end_.Container(); }
  unsigned endOffset() const { return end_.Offset(); }

  bool collapsed() const { return start_ == end_; }
  bool IsConnected() const;

  Node* commonAncestorContainer() const;
  static Node* commonAncestorContainer(const Node* container_a,
                                       const Node* container_b);
  void setStart(Node* container,
                unsigned offset,
                ExceptionState& = ASSERT_NO_EXCEPTION);
  void setEnd(Node* container,
              unsigned offset,
              ExceptionState& = ASSERT_NO_EXCEPTION);
  void collapse(bool to_start);
  bool isPointInRange(Node* ref_node, unsigned offset, ExceptionState&) const;
  short comparePoint(Node* ref_node, unsigned offset, ExceptionState&) const;
  enum CompareResults {
    NODE_BEFORE,
    NODE_AFTER,
    NODE_BEFORE_AND_AFTER,
    NODE_INSIDE
  };
  enum CompareHow { kStartToStart, kStartToEnd, kEndToEnd, kEndToStart };
  short compareBoundaryPoints(unsigned how,
                              const Range* source_range,
                              ExceptionState&) const;
  static short compareBoundaryPoints(Node* container_a,
                                     unsigned offset_a,
                                     Node* container_b,
                                     unsigned offset_b,
                                     ExceptionState&);
  static short compareBoundaryPoints(const RangeBoundaryPoint& boundary_a,
                                     const RangeBoundaryPoint& boundary_b,
                                     ExceptionState&);
  bool BoundaryPointsValid() const;
  bool intersectsNode(Node* ref_node, ExceptionState&);
  void deleteContents(ExceptionState&);
  DocumentFragment* extractContents(ExceptionState&);
  DocumentFragment* cloneContents(ExceptionState&);
  void insertNode(Node*, ExceptionState&);
  String toString() const;

  String GetText() const;

  DocumentFragment* createContextualFragment(const String& html,
                                             ExceptionState&);

  void detach();
  Range* cloneRange() const;

  void setStartAfter(Node*, ExceptionState& = ASSERT_NO_EXCEPTION);
  void setEndBefore(Node*, ExceptionState& = ASSERT_NO_EXCEPTION);
  void setEndAfter(Node*, ExceptionState& = ASSERT_NO_EXCEPTION);
  void selectNode(Node*, ExceptionState& = ASSERT_NO_EXCEPTION);
  void selectNodeContents(Node*, ExceptionState&);
  static bool selectNodeContents(Node*, Position&, Position&);
  void surroundContents(Node*, ExceptionState&);
  void setStartBefore(Node*, ExceptionState& = ASSERT_NO_EXCEPTION);

  const Position StartPosition() const { return start_.ToPosition(); }
  const Position EndPosition() const { return end_.ToPosition(); }
  void setStart(const Position&, ExceptionState& = ASSERT_NO_EXCEPTION);
  void setEnd(const Position&, ExceptionState& = ASSERT_NO_EXCEPTION);

  Node* FirstNode() const;
  Node* PastLastNode() const;

  // Not transform-friendly
  IntRect BoundingBox() const;

  // Transform-friendly
  void TextQuads(Vector<FloatQuad>&, bool use_selection_height = false) const;
  void GetBorderAndTextQuads(Vector<FloatQuad>&) const;
  FloatRect BoundingRect() const;

  void NodeChildrenWillBeRemoved(ContainerNode&);
  void NodeWillBeRemoved(Node&);

  void DidInsertText(const CharacterData&, unsigned offset, unsigned length);
  void DidRemoveText(const CharacterData&, unsigned offset, unsigned length);
  void DidMergeTextNodes(const NodeWithIndex& old_node, unsigned offset);
  void DidSplitTextNode(const Text& old_node);
  void UpdateOwnerDocumentIfNeeded();

  // Expand range to a unit (word or sentence or block or document) boundary.
  // Please refer to https://bugs.webkit.org/show_bug.cgi?id=27632 comment #5
  // for details.
  void expand(const String&, ExceptionState&);

  ClientRectList* getClientRects() const;
  ClientRect* getBoundingClientRect() const;

  static Node* CheckNodeWOffset(Node*, unsigned offset, ExceptionState&);

  DECLARE_TRACE();

 private:
  explicit Range(Document&);
  Range(Document&,
        Node* start_container,
        unsigned start_offset,
        Node* end_container,
        unsigned end_offset);

  void SetDocument(Document&);

  void CheckNodeBA(Node*, ExceptionState&) const;
  void CheckExtractPrecondition(ExceptionState&);
  bool HasSameRoot(const Node&) const;

  enum ActionType { DELETE_CONTENTS, EXTRACT_CONTENTS, CLONE_CONTENTS };
  DocumentFragment* ProcessContents(ActionType, ExceptionState&);
  static Node* ProcessContentsBetweenOffsets(ActionType,
                                             DocumentFragment*,
                                             Node*,
                                             unsigned start_offset,
                                             unsigned end_offset,
                                             ExceptionState&);
  static void ProcessNodes(ActionType,
                           HeapVector<Member<Node>>&,
                           Node* old_container,
                           Node* new_container,
                           ExceptionState&);
  enum ContentsProcessDirection {
    kProcessContentsForward,
    kProcessContentsBackward
  };
  static Node* ProcessAncestorsAndTheirSiblings(ActionType,
                                                Node* container,
                                                ContentsProcessDirection,
                                                Node* cloned_container,
                                                Node* common_root,
                                                ExceptionState&);
  void UpdateSelectionIfAddedToSelection();
  void RemoveFromSelectionIfInDifferentRoot(Document& old_document);

  Member<Document> owner_document_;  // Cannot be null.
  RangeBoundaryPoint start_;
  RangeBoundaryPoint end_;

  friend class RangeUpdateScope;
};

CORE_EXPORT bool AreRangesEqual(const Range*, const Range*);

using RangeVector = HeapVector<Member<Range>>;

}  // namespace blink

#ifndef NDEBUG
// Outside the WebCore namespace for ease of invocation from gdb.
void showTree(const blink::Range*);
#endif

#endif  // Range_h
