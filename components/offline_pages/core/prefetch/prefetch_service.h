// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_SERVICE_H_
#define COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_SERVICE_H_

#include "components/keyed_service/core/keyed_service.h"
#include "components/offline_pages/core/offline_page_item.h"

class GURL;

namespace offline_pages {

// Main class for the Offline Pages Prefetching feature, which will ask a server
// to package content from interesting URLs and send it to Chrome for
// consumption while offline.
class PrefetchService : public KeyedService {
 public:
  struct PrefetchURL {
    // Used to differentiate URLs from different sources.  |name_space| should
    // be unique per source. |id| can be anything useful to identify the page,
    // but will not be used for deduplication.
    ClientId client_id;

    // This URL will be prefetched by the service.
    GURL url;
  };

  ~PrefetchService() override = default;

  // Called when a consumer has candidate URLs for the system to prefetch.
  // Duplicates are accepted by the PrefetchService but ignored.
  virtual void AddCandidatePrefetchURLs(
      const std::vector<PrefetchURL>& prefetch_urls) = 0;

  // Called when all existing suggestions are no longer considered valid for a
  // given namespace.  The prefetch service should remove any URLs that
  // have not yet started downloading within that namespace.
  virtual void RemoveAllUnprocessedPrefetchURLs(
      const std::string& name_space) = 0;

  // Called to invalidate a single PrefetchURL entry identified by |client_id|.
  // If multiple have the same |client_id|, they will all be removed.
  virtual void RemovePrefetchURLsByClientId(const ClientId& client_id) = 0;
};

}  // namespace offline_pages

#endif  // COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_SERVICE_IMPL_H_
