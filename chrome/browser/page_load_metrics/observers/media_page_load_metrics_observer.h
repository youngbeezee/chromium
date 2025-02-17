// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_MEDIA_PAGE_LOAD_METRICS_OBSERVER_H_
#define CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_MEDIA_PAGE_LOAD_METRICS_OBSERVER_H_

#include <stdint.h>

#include "base/macros.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_observer.h"
#include "content/public/browser/web_contents_observer.h"

namespace page_load_metrics {
struct PageLoadExtraInfo;
struct PageLoadTiming;
}

// Observer responsible for recording metrics on pages that play at least one
// MEDIA request.
class MediaPageLoadMetricsObserver
    : public page_load_metrics::PageLoadMetricsObserver {
 public:
  MediaPageLoadMetricsObserver();
  ~MediaPageLoadMetricsObserver() override;

  // page_load_metrics::PageLoadMetricsObserver:
  void OnComplete(const page_load_metrics::PageLoadTiming& timing,
                  const page_load_metrics::PageLoadExtraInfo& info) override;
  page_load_metrics::PageLoadMetricsObserver::ObservePolicy
  FlushMetricsOnAppEnterBackground(
      const page_load_metrics::PageLoadTiming& timing,
      const page_load_metrics::PageLoadExtraInfo& info) override;
  void OnLoadedResource(const page_load_metrics::ExtraRequestCompleteInfo&
                            extra_request_complete_info) override;
  void MediaStartedPlaying(
      const content::WebContentsObserver::MediaPlayerInfo& video_type,
      bool is_in_main_frame) override;

 private:
  // Records histograms for byte information.
  void RecordByteHistograms();

  // The number of body (not header) prefilter bytes consumed by requests for
  // the page.
  int64_t cache_bytes_;
  int64_t network_bytes_;

  // Whether the page load played a media element.
  bool played_media_;

  DISALLOW_COPY_AND_ASSIGN(MediaPageLoadMetricsObserver);
};

#endif  // CHROME_BROWSER_PAGE_LOAD_METRICS_OBSERVERS_MEDIA_PAGE_LOAD_METRICS_OBSERVER_H_
